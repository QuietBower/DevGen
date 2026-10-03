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

#define TYPE_PCIBASE_DEVICE "snd_ymfpci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define YMF_VENDOR_ID          0x1073
#define YMF_DEVICE_ID          0x0004
#define YMF_CLASS_ID           0x040100  /* PCI_CLASS_MULTIMEDIA_AUDIO */

/* BAR configuration */
#define BAR0_SIZE              0x8000

/* MMIO Register offsets from ymfpci.c */
#define YDSXGR_SPDIFLOOPVOL        0x00BC
#define YDSXGR_SPDIFOUTVOL         0x00B8
#define YDSXGR_NATIVEDACOUTVOL     0x0084
#define YDSXGR_ZVLOOPVOL           0x009C
#define YDSXGR_LEGACYOUTVOL        0x0080
#define YDSXGR_NATIVEDACINVOL      0x00AC
#define YDSXGR_SECADCLOOPVOL       0x00A0
#define YDSXGR_NATIVEDACLOOPVOL    0x0098
#define YDSXGR_SPDIFINCTRL         0x0034
#define YDSXGR_PRIADCOUTVOL        0x0090
#define YDSXGR_PRIADCLOOPVOL       0x00A4
#define YDSXGR_ZVOUTVOL            0x0088
#define YDSXGR_SPDIFOUTCTRL        0x0018
#define YDSXGR_NATIVEADCINVOL      0x00A8
#define YDSXGR_SECADCOUTVOL        0x008C
#define YDSXGR_AC97CMDADR          0x0062
#define YDSXGR_PRISTATUSDATA       0x0064
#define YDSXG_AC97READCMD          0x8000
#define YDSXG_AC97WRITECMD         0x0000
#define YDSXGR_AC97CMDDATA         0x0060
#define YDSXG_CTRLLENGTH           0x3000
#define YDSXG_DSPLENGTH            0x0080
#define YDSXGR_MAPOFEFFECT         0x0154
#define YDSXGR_PLAYCTRLBASE        0x0158
#define YDSXGR_EFFCTRLBASE         0x0160
#define YDSXGR_RECCTRLBASE         0x015C
#define YDSXGR_STATUS              0x0100
#define YDSXGR_WORKSIZE            0x014C
#define YDSXGR_BUF441OUTVOL        0x00B0
#define YDSXGR_GLOBALCTRL          0x0008
#define YDSXGR_WORKBASE            0x0164
#define PCIR_DSXG_CTRL             0x48
#define YDSXGR_DSPINSTRAM          0x1000
#define YDSXGR_MAPOFREC            0x0150
#define YDSXGR_CTRLINSTRAM         0x4000
#define YDSXGR_MODE                0x0108
#define YDSXGR_SECSTATUSADR        0x006A
#define YDSXGR_PRISTATUSADR        0x0066
#define YDSXGR_SPDIFOUTSTATUS      0x001C
#define YDSXGR_EFFCTRLSIZE         0x0148
#define YDSXGR_RECCTRLSIZE         0x0144
#define YDSXG_DEFAULT_WORK_SIZE    0x0400
#define YDSXGR_PLAYCTRLSIZE        0x0140
#define YDSXGR_RECSLOTSR           0x00C4
#define YDSXGR_RECFORMAT           0x00CC
#define YDSXGR_ADCSLOTSR           0x00C0
#define YDSXGR_ADCFORMAT           0x00C8
#define PCIR_DSXG_SBBASE           0x62
#define YDSXGR_TIMERCTRL           0x0010
#define YDSXGR_TIMERCOUNT          0x0012
#define YDSXGR_CONFIG              0x0114

/* PCI configuration legacy registers (vendor-specific) */
#define PCIR_DSXG_ELEGACY          0x42
#define YMFPCI_LEGACY_JPEN         (1 << 2)
#define PCIR_DSXG_LEGACY           0x40
#define PCIR_DSXG_JOYBASE          0x66
#define YMFPCI_LEGACY_MEN          (1 << 3)
#define YMFPCI_LEGACY2_IMOD        (1 << 15)
#define YMFPCI_LEGACY_MIEN         (1 << 4)
#define YMFPCI_LEGACY2_MPUIO       (3 << 4)
#define YMFPCI_LEGACY_FMEN         (1 << 1)
#define YMFPCI_LEGACY2_FMIO        (3 << 0)
#define PCIR_DSXG_FMBASE           0x60
#define PCIR_DSXG_MPU401BASE       0x64


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
    uint32_t regs[BAR0_SIZE / sizeof(uint32_t)];  /* flat MMIO register shadow */

    /* DMA Context */
    dma_addr_t playback_ctrl_base;
    dma_addr_t capture_ctrl_base;
    dma_addr_t effect_ctrl_base;
    dma_addr_t work_base;
    uint32_t work_size;
    uint32_t bank_size_playback;
    uint32_t bank_size_capture;
    uint32_t bank_size_effect;

    uint32_t status_reg;       /* YDSXGR_STATUS */

    QEMUTimer *timer;          /* hardware timer if needed */

    /* AC'97 codec emulation */
    uint16_t ac97_regs[0x80];
    uint16_t ac97_cmd_addr;
    uint16_t ac97_last_read;
    uint16_t ac97_pending_data; /* buffer for write command data */
};

#define CODEC_READY_MASK  0x00000001

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == YDSXGR_PRISTATUSDATA) {
        return s->ac97_last_read;
    }
    uint32_t reg = s->regs[addr >> 2];
    if (addr < BAR0_SIZE) {
        switch (size) {
        case 1: return (reg >> (8 * (addr & 3))) & 0xFF;
        case 2: return (reg >> (8 * (addr & 2))) & 0xFFFF;
        case 4: return reg;
        case 8: {
            uint64_t val = reg;
            if (addr + 4 < BAR0_SIZE) {
                val |= (uint64_t)s->regs[(addr >> 2) + 1] << 32;
            }
            return val;
        }
        default: return ~0ULL;
        }
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read at 0x%" HWADDR_PRIx " size %d\n", __func__, addr, size);
    return ~0ULL;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == YDSXGR_AC97CMDADR) {
        uint16_t cmd = val & 0xFFFF;
        uint8_t reg = cmd & 0x7F;
        if (cmd & 0x8000) {
            /* Read command: read from ac97_regs[reg] */
            s->ac97_last_read = s->ac97_regs[reg];
            s->regs[YDSXGR_STATUS >> 2] |= CODEC_READY_MASK;
        } else {
            /* Write command: write pending_data to ac97_regs[reg], except reset register (0x00) */
            if (reg != 0x00) {
                s->ac97_regs[reg] = s->ac97_pending_data;
            } /* else: writes to reset register are ignored, it always reads back 0x0090 */
            s->regs[YDSXGR_STATUS >> 2] |= CODEC_READY_MASK;
        }
        /* Update shadow register */
        s->regs[addr >> 2] = (s->regs[addr >> 2] & 0xFFFF0000) | cmd;
        return;
    }
    if (addr == YDSXGR_AC97CMDDATA) {
        uint16_t data = val & 0xFFFF;
        /* Buffer the data for the next write command */
        s->ac97_pending_data = data;
        /* Update shadow register */
        s->regs[addr >> 2] = (s->regs[addr >> 2] & 0xFFFF0000) | data;
        return;
    }
    if (addr < BAR0_SIZE) {
        uint32_t mask = (size == 1) ? 0xFF : (size == 2) ? 0xFFFF : 0xFFFFFFFF;
        uint32_t shift = 8 * (addr & 3);
        uint32_t new_val = (uint32_t)val & mask;
        s->regs[addr >> 2] &= ~(mask << shift);
        s->regs[addr >> 2] |= new_val << shift;
        if (size == 8) {
            if (addr + 4 < BAR0_SIZE) {
                mask = 0xFFFFFFFF;
                shift = 0;
                new_val = (uint32_t)(val >> 32);
                s->regs[(addr >> 2) + 1] &= ~mask;
                s->regs[(addr >> 2) + 1] |= new_val;
            }
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write at 0x%" HWADDR_PRIx " size %d\n", __func__, addr, size);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used */
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
    /* Initialize AC'97 codec registers */
    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    s->ac97_regs[0x00] = 0x0090;   /* Reset register ID: fixed value indicating codec revision/capabilities */
    s->ac97_regs[0x7c] = 0x594d;   /* Vendor ID1 (Yamaha) */
    s->ac97_regs[0x7e] = 0x4800;   /* Vendor ID2 */
    s->ac97_regs[0x26] = 0x0010;   /* Powerdown Ctrl/Stat: Codec Ready bit set */
    s->ac97_regs[0x28] = 0x0011;   /* Extended Audio ID: VRA + SDAC */
    s->ac97_cmd_addr = 0;
    s->ac97_last_read = 0;
    s->ac97_pending_data = 0;
    s->regs[YDSXGR_STATUS >> 2] = CODEC_READY_MASK;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  YMF_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  YMF_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, YMF_CLASS_ID );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "ymfpci-bar0" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X; driver uses legacy IRQ */

    /* No DMA configuration needed (QEMU provides implicit DMA via pci_dma_* functions) */

    /* No timer needed */

    /* Initialize register shadow to zero and AC'97 defaults via reset */
    pcibase_reset(DEVICE(pdev));
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

    /* No extra cleanup needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_ymfpci_pci",
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
