/*
 * QEMU Riptide sound card PCI device emulation (QEMU 8.2.10).
 * Generated from Linux driver snd_riptide.
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

#define TYPE_PCIBASE_DEVICE "snd_riptide_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_RIPTIDE       0x127a
#define PCI_DEVICE_ID_RIPTIDE       0x4310

/* Register offsets */
#define REG_AUDIO_CONTROL   0x00
#define REG_AUDIO_STATUS    0x04
#define REG_CMD_PORT0_DATA1 0x10
#define REG_CMD_PORT0_DATA2 0x14
#define REG_CMD_PORT0_STAT  0x18
#define REG_CMD_PORT1_DATA1 0x30
#define REG_CMD_PORT1_DATA2 0x34
#define REG_CMD_PORT1_STAT  0x38

/* Audio control register bits */
#define AUDIO_CONTROL_GRESET    (1 << 0)
#define AUDIO_CONTROL_AIE       (1 << 2)
#define AUDIO_CONTROL_AIACK     (1 << 3)
#define AUDIO_CONTROL_ECMDAE    (1 << 4)
#define AUDIO_CONTROL_ECMDBE    (1 << 5)
#define AUDIO_CONTROL_EDATAF    (1 << 6)
#define AUDIO_CONTROL_EDATBF    (1 << 7)
#define AUDIO_CONTROL_ESBIRQON  (1 << 8)
#define AUDIO_CONTROL_EMPUIRQ   (1 << 9)

/* Audio status register bits */
#define AUDIO_STATUS_READY      (1 << 0)
#define AUDIO_STATUS_DLREADY    (1 << 1)
#define AUDIO_STATUS_DLERR      (1 << 2)
#define AUDIO_STATUS_GERR       (1 << 3)
#define AUDIO_STATUS_CMDAEIRQ   (1 << 4)
#define AUDIO_STATUS_CMDBEIRQ   (1 << 5)
#define AUDIO_STATUS_DATAFIRQ   (1 << 6)
#define AUDIO_STATUS_DATBFIRQ   (1 << 7)
#define AUDIO_STATUS_EOBIRQ     (1 << 8)
#define AUDIO_STATUS_EOSIRQ     (1 << 9)
#define AUDIO_STATUS_EOCIRQ     (1 << 10)
#define AUDIO_STATUS_UNSLIRQ    (1 << 11)
#define AUDIO_STATUS_SBIRQ      (1 << 12)
#define AUDIO_STATUS_MPUIRQ     (1 << 13)

/* Command port status bits */
#define CMD_STAT_CMDE           (1 << 0)
#define CMD_STAT_DATF           (1 << 1)

/* PCI extended registers from driver source */
#define PCI_EXT_FM_BASE     0x4A
#define PCI_EXT_MPU_BASE    0x4C
#define PCI_EXT_GAME_BASE   0x4E
#define PCI_EXT_LEGACY_MASK 0x50

/* BAR0 size (power-of-2) */
#define BAR0_SIZE 256

/* Hardware register map matching the driver's riptideport layout */
struct riptide_regs {
    uint32_t audio_control;   /* offset 0x00 */
    uint32_t audio_status;    /* offset 0x04 */
    uint32_t reserved[2];     /* offset 0x08-0x0F */
    struct {
        uint32_t data1;       /* offset 0x10 / 0x30 */
        uint32_t data2;       /* offset 0x14 / 0x34 */
        uint32_t stat;        /* offset 0x18 / 0x38 */
        uint32_t pad[5];      /* offset 0x1C-0x2F / 0x3C-0x4F */
    } cmd_port[2];
};

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
    uint32_t audio_control;
    uint32_t audio_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct riptide_regs regs;

    /* DMA Context */
    struct {
        dma_addr_t addr;
        uint32_t count;
    } dma;

    uint32_t status;            /* Operational status flags */
    bool in_reset;              /* State used to handle reset sequences */
    uint8_t power_state;        /* Power management state (D0-D3) */
    uint32_t extra;             /* Additional placeholder state */

    /* PCI extended legacy resource registers */
    uint16_t mpu_base;   /* MPU-401 base at 0x4C */
    uint16_t fm_base;    /* FM base at 0x4A */
    uint16_t game_base;  /* Joystick base at 0x4E */
    uint8_t  legacy_mask;/* Legacy mask at 0x50 */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Driver uses AIE and AIACK to control interrupts, but during probe
     * no DMA streams are active, so we leave IRQ deasserted.
     */
    pci_set_irq(pdev, 0);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev; /* Suppress unused warning */

    /* The driver uses scatter-gather DMA for audio streams, but we don't
     * emulate actual transfers because the probe does not start any stream.
     * Keep this stub for potential future use.
     */
}

/* MMIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_AUDIO_CONTROL:
        val = s->regs.audio_control;
        break;
    case REG_AUDIO_STATUS:
        val = s->regs.audio_status;
        break;
    case REG_CMD_PORT0_DATA1:
        val = s->regs.cmd_port[0].data1;
        break;
    case REG_CMD_PORT0_DATA2:
        val = s->regs.cmd_port[0].data2;
        /* Clearing DATF after reading data2 mimics hardware behavior. */
        s->regs.cmd_port[0].stat &= ~CMD_STAT_DATF;
        break;
    case REG_CMD_PORT0_STAT:
        val = s->regs.cmd_port[0].stat;
        break;
    case REG_CMD_PORT1_DATA1:
        val = s->regs.cmd_port[1].data1;
        break;
    case REG_CMD_PORT1_DATA2:
        val = s->regs.cmd_port[1].data2;
        /* Clearing DATF after reading data2. */
        s->regs.cmd_port[1].stat &= ~CMD_STAT_DATF;
        break;
    case REG_CMD_PORT1_STAT:
        val = s->regs.cmd_port[1].stat;
        break;
    default:
        if (addr < BAR0_SIZE) {
            /* Read from unused registers returns 0. */
            val = 0;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: MMIO read out of range: 0x"HWADDR_FMT_plx"\n",
                          __func__, addr);
            val = ~0ULL;
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_AUDIO_CONTROL:
        s->regs.audio_control = val;
        /* Handle GRESET: writing 1 triggers reset, 0 clears it.
         * During reset, we clear GERR and set READY after clear of GRESET.
         */
        if (val & AUDIO_CONTROL_GRESET) {
            s->regs.audio_status &= ~AUDIO_STATUS_READY;
            s->regs.audio_status |= AUDIO_STATUS_GERR;
        } else {
            s->regs.audio_status &= ~AUDIO_STATUS_GERR;
            s->regs.audio_status |= AUDIO_STATUS_READY;
        }
        /* AIACK is write-1-to-clear; writing 1 clears interrupt status bits. */
        if (val & AUDIO_CONTROL_AIACK) {
            s->regs.audio_status &= ~(AUDIO_STATUS_EOBIRQ | AUDIO_STATUS_EOSIRQ |
                                     AUDIO_STATUS_EOCIRQ | AUDIO_STATUS_MPUIRQ |
                                     AUDIO_STATUS_SBIRQ);
        }
        break;
    case REG_AUDIO_STATUS:
        /* Audio status is read-only; ignore writes. */
        break;
    case REG_CMD_PORT0_DATA1:
        s->regs.cmd_port[0].data1 = val;
        /* Simulate command acceptance and immediate completion.
         * If the opcode is 0 (GETV), return a fake firmware version.
         * Otherwise, provide a dummy success response.
         */
        if (val == 0) {
            /* GETV: return version 1.0 */
            s->regs.cmd_port[0].data1 = 0x00010000;
        } else {
            s->regs.cmd_port[0].data1 = 0;
        }
        s->regs.cmd_port[0].data2 = 0;
        s->regs.cmd_port[0].stat = CMD_STAT_CMDE | CMD_STAT_DATF;
        break;
    case REG_CMD_PORT0_DATA2:
        s->regs.cmd_port[0].data2 = val;
        break;
    case REG_CMD_PORT0_STAT:
        /* Stat is read-only; ignore writes. */
        break;
    case REG_CMD_PORT1_DATA1:
        s->regs.cmd_port[1].data1 = val;
        if (val == 0) {
            s->regs.cmd_port[1].data1 = 0x00010000;
        } else {
            s->regs.cmd_port[1].data1 = 0;
        }
        s->regs.cmd_port[1].data2 = 0;
        s->regs.cmd_port[1].stat = CMD_STAT_CMDE | CMD_STAT_DATF;
        break;
    case REG_CMD_PORT1_DATA2:
        s->regs.cmd_port[1].data2 = val;
        break;
    case REG_CMD_PORT1_STAT:
        /* Stat is read-only; ignore writes. */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: MMIO write out of range: 0x"HWADDR_FMT_plx" val=0x"PRIx64"\n",
                      __func__, addr, val);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* Expect 32-bit register access */
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* PCI config space override to handle driver-specific legacy registers */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Intercept the legacy base address and mask registers */
    switch (addr) {
    case PCI_EXT_FM_BASE:
        if (len == 2) return s->fm_base;
        break;
    case PCI_EXT_MPU_BASE:
        if (len == 2) return s->mpu_base;
        break;
    case PCI_EXT_GAME_BASE:
        if (len == 2) return s->game_base;
        break;
    case PCI_EXT_LEGACY_MASK:
        if (len == 1) return s->legacy_mask;
        break;
    default:
        break;
    }
    /* Fallback to default PCI config read */
    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Intercept writes to legacy resource registers */
    switch (addr) {
    case PCI_EXT_FM_BASE:
        if (len == 2) { s->fm_base = val; return; }
        break;
    case PCI_EXT_MPU_BASE:
        if (len == 2) { s->mpu_base = val; return; }
        break;
    case PCI_EXT_GAME_BASE:
        if (len == 2) { s->game_base = val; return; }
        break;
    case PCI_EXT_LEGACY_MASK:
        if (len == 1) { s->legacy_mask = val; return; }
        break;
    default:
        break;
    }
    /* Fallback to default PCI config write */
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->audio_control = 0;
    s->audio_status = AUDIO_STATUS_READY; /* Ready after reset */
    s->in_reset = false;
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.audio_status = AUDIO_STATUS_READY;
    /* Command ports initially empty (CMDE=0, DATF=0) */
    s->regs.cmd_port[0].stat = 0;
    s->regs.cmd_port[1].stat = 0;

    /* Initialize extended resource registers to 0 (driver may program them) */
    s->fm_base = 0;
    s->mpu_base = 0;
    s->game_base = 0;
    s->legacy_mask = 0;
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
        /* Not used; retained for completeness. */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_RIPTIDE);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_RIPTIDE);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: driver expects BAR0 as MMIO */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "riptide-mmio";
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Device does not support MSI/MSI-X; only legacy INTx */

    /* Reset the device to initial state */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s; /* Suppress unused warning */
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* Free resources if any */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_riptide_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(audio_control, PCIBaseState),
        VMSTATE_UINT32(audio_status, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize      = pcibase_realize;
    k->exit         = pcibase_uninit;
    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;
    dc->reset       = pcibase_reset;
    dc->vmsd        = &vmstate_pcibase;
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