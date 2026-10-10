/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Designed for register-level modeling and PCIe driver probing.
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

#define TYPE_PCIBASE_DEVICE "snd_azt3328_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define AZF_VENDOR_ID 0x122D
#define AZF_DEVICE_ID 0x50DC
#define AZF_CLASS_ID  PCI_CLASS_MULTIMEDIA_AUDIO

#define AZF_IO_SIZE_CTRL 0x80
#define AZF_IO_SIZE_MIXER 0x40
#define AZF_IO_OFFS_CODEC_PLAYBACK 0x00
#define AZF_IO_OFFS_CODEC_CAPTURE 0x20
#define AZF_IO_OFFS_CODEC_I2S_OUT 0x40

#define IDX_IO_CODEC_DMA_FLAGS 0x00
#define IDX_IO_CODEC_IRQTYPE 0x02
#define IDX_IO_CODEC_DMA_START_1 0x04
#define IDX_IO_CODEC_DMA_CURRPOS 0x10
#define IDX_IO_CODEC_SOUNDFORMAT 0x16
#define IDX_IO_TIMER_VALUE 0x60
#define IDX_IO_IRQSTATUS 0x64
#define IDX_IO_6AH 0x6A

#define DMA_RESUME			0x0001
#define DMA_RUN_SOMETHING1		0x0002
#define DMA_RUN_SOMETHING2		0x0004
#define DMA_EPILOGUE_SOMETHING	0x0010
#define DMA_SOMETHING_ELSE		0x0020

#define TIMER_VALUE_MASK		0x000fffffUL
#define TIMER_COUNTDOWN_ENABLE	0x01000000UL
#define TIMER_IRQ_ENABLE		0x02000000UL

#define IRQ_PLAYBACK 0x0001
#define IRQ_RECORDING 0x0002
#define IRQ_I2S_OUT 0x0004
#define IRQ_GAMEPORT 0x0008
#define IRQ_MPU401 0x0010
#define IRQ_TIMER 0x0020

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
    uint32_t irq_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t ctrl_regs[128];
    uint8_t game_regs[8];
    uint8_t mpu_regs[4];
    uint8_t opl3_regs[8];
    uint8_t mixer_regs[64];
    uint16_t shadow_reg_ctrl_6AH;

    /* DMA Context */
    struct {
        uint32_t dma_start_1;
        uint32_t dma_start_2;
        uint32_t dma_lengths;
        bool running;
    } codec_dma[3];

    uint32_t saved_regs_ctrl[128 / 4];
    uint32_t saved_regs_game[8 / 4];
    uint32_t saved_regs_mpu[4 / 4];
    uint32_t saved_regs_opl3[8 / 4];
    uint32_t saved_regs_mixer[64 / 4];

    QEMUTimer *timer;
};

enum snd_azf3328_codec_type {
    AZF_CODEC_PLAYBACK = 0,
    AZF_CODEC_CAPTURE = 1,
    AZF_CODEC_I2S_OUT = 2,
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->irq_status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x60) {
        int codec = addr / 0x20;
        int reg = addr % 0x20;
        if (reg == IDX_IO_CODEC_DMA_CURRPOS) {
            return s->codec_dma[codec].dma_start_1;
        }
    } else if (addr == IDX_IO_IRQSTATUS) {
        return s->irq_status;
    } else if (addr == IDX_IO_6AH) {
        return s->shadow_reg_ctrl_6AH;
    }

    if (addr + size <= sizeof(s->ctrl_regs)) {
        memcpy(&val, &s->ctrl_regs[addr], size);
    }
    return val;
}

static void pcibase_ctrl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size <= sizeof(s->ctrl_regs)) {
        memcpy(&s->ctrl_regs[addr], &val, size);
    }

    if (addr < 0x60) {
        int codec = addr / 0x20;
        int reg = addr % 0x20;
        if (reg == IDX_IO_CODEC_IRQTYPE) {
            s->irq_status &= ~(1 << codec);
            pcibase_update_irq(s);
        } else if (reg == IDX_IO_CODEC_DMA_START_1) {
            s->codec_dma[codec].dma_start_1 = val;
        } else if (reg == IDX_IO_CODEC_DMA_START_1 + 4) {
            s->codec_dma[codec].dma_start_2 = val;
        } else if (reg == IDX_IO_CODEC_DMA_START_1 + 8) {
            s->codec_dma[codec].dma_lengths = val;
        }
    } else if (addr == IDX_IO_TIMER_VALUE + 3) {
        if (val == 0x07) {
            s->irq_status &= ~IRQ_TIMER;
            pcibase_update_irq(s);
        }
    } else if (addr == IDX_IO_6AH) {
        s->shadow_reg_ctrl_6AH = val;
    }
}

static uint64_t pcibase_game_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr + size <= sizeof(s->game_regs)) memcpy(&val, &s->game_regs[addr], size);
    return val;
}

static void pcibase_game_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr + size <= sizeof(s->game_regs)) memcpy(&s->game_regs[addr], &val, size);
}

static uint64_t pcibase_mpu_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr + size <= sizeof(s->mpu_regs)) memcpy(&val, &s->mpu_regs[addr], size);
    return val;
}

static void pcibase_mpu_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr + size <= sizeof(s->mpu_regs)) memcpy(&s->mpu_regs[addr], &val, size);
}

static uint64_t pcibase_opl3_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr + size <= sizeof(s->opl3_regs)) memcpy(&val, &s->opl3_regs[addr], size);
    return val;
}

static void pcibase_opl3_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr + size <= sizeof(s->opl3_regs)) memcpy(&s->opl3_regs[addr], &val, size);
}

static uint64_t pcibase_mixer_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr + size <= sizeof(s->mixer_regs)) memcpy(&val, &s->mixer_regs[addr], size);
    return val;
}

static void pcibase_mixer_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr + size <= sizeof(s->mixer_regs)) memcpy(&s->mixer_regs[addr], &val, size);
}

static const MemoryRegionOps pcibase_pio_ops[5] = {
    [0] = { .read = pcibase_ctrl_read, .write = pcibase_ctrl_write, .endianness = DEVICE_LITTLE_ENDIAN, .valid = { .min_access_size = 1, .max_access_size = 4 } },
    [1] = { .read = pcibase_game_read, .write = pcibase_game_write, .endianness = DEVICE_LITTLE_ENDIAN, .valid = { .min_access_size = 1, .max_access_size = 4 } },
    [2] = { .read = pcibase_mpu_read,  .write = pcibase_mpu_write,  .endianness = DEVICE_LITTLE_ENDIAN, .valid = { .min_access_size = 1, .max_access_size = 4 } },
    [3] = { .read = pcibase_opl3_read, .write = pcibase_opl3_write, .endianness = DEVICE_LITTLE_ENDIAN, .valid = { .min_access_size = 1, .max_access_size = 4 } },
    [4] = { .read = pcibase_mixer_read,.write = pcibase_mixer_write,.endianness = DEVICE_LITTLE_ENDIAN, .valid = { .min_access_size = 1, .max_access_size = 4 } },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->irq_status = 0;
    memset(s->ctrl_regs, 0, sizeof(s->ctrl_regs));
    memset(s->game_regs, 0, sizeof(s->game_regs));
    memset(s->mpu_regs, 0, sizeof(s->mpu_regs));
    memset(s->opl3_regs, 0, sizeof(s->opl3_regs));
    memset(s->mixer_regs, 0, sizeof(s->mixer_regs));
    s->shadow_reg_ctrl_6AH = 0;
    memset(s->codec_dma, 0, sizeof(s->codec_dma));
    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops[bi->index], s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x122D );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x50DC );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO );
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
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_PIO, AZF_IO_SIZE_CTRL, "ctrl_io"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_PIO, 8, "game_io"};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_PIO, 4, "mpu_io"};
    s->bar_info[3] = (BARInfo){3, BAR_TYPE_PIO, 8, "opl3_io"};
    s->bar_info[4] = (BARInfo){4, BAR_TYPE_PIO, AZF_IO_SIZE_MIXER, "mixer_io"};

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_azt3328_pci",
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
