
/*
 * QEMU PCI device model for snd-lx6464es driver.
 * This file is generated based on available driver source.
 * It emulates the PLX and DSP interface at a basic level.
 * Full functionality requires additional driver-specific symbols.
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

#define TYPE_PCIBASE_DEVICE "snd_lx6464es_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs extracted from driver pci_device_id table (first entry) */
#define VENDOR_ID 0x10b5      /* PCI_VENDOR_ID_PLX */
#define DEVICE_ID 0x9056      /* PCI_DEVICE_ID_PLX_9056 */
/* Subsystem IDs needed for matching; values to be supplied */
#ifndef PCI_VENDOR_ID_DIGIGRAM
#define PCI_VENDOR_ID_DIGIGRAM 0x1369  /* common, may need update */
#endif
#define PCI_SUBDEVICE_ID_DIGIGRAM_LX6464ES_SERIAL_SUBSYSTEM 0xc001
#define SUBSYSTEM_VENDOR_ID PCI_VENDOR_ID_DIGIGRAM
#define SUBSYSTEM_ID PCI_SUBDEVICE_ID_DIGIGRAM_LX6464ES_SERIAL_SUBSYSTEM

#define CLASS_ID 0x0401  /* Multimedia Audio Device */

/* -------------------------------------------------------------------- */
/* DSP and PLX Register Definitions (offsets derived from driver source) */
/* -------------------------------------------------------------------- */

/* DSP MMIO port indices (multiplied by 4 to get byte offset) */
#define DSP_PORT_CRM1     0   /* Command mailbox base: dsp_cmd_regs[0..11] */
#define DSP_PORT_CRM2    12   /* Status mailbox base: dsp_stat_regs[0..11] */
#define DSP_PORT_CSM     24   /* Control/Status register */

/* DSP MMIO byte offsets */
#define DSP_CRM1_BYTE_OFF  (DSP_PORT_CRM1 * 4)
#define DSP_CRM2_BYTE_OFF  (DSP_PORT_CRM2 * 4)
#define DSP_CSM_BYTE_OFF   (DSP_PORT_CSM  * 4)

/* DSP CSM bits (MicroBlaze control/status) */
#define Reg_CSM_MC   (1 << 0)   /* MicroBlaze Command */
#define Reg_CSM_MR   (1 << 1)   /* MicroBlaze Response */

/* PLX I/O register offsets (matches driver's plx_port_offsets) */
#define PLX_OFFSET_IRQCS    0x00   /* IRQ Control/Status */
#define PLX_OFFSET_INTRSTAT 0x04   /* Interrupt Source Status (W1C) */
#define PLX_OFFSET_CHIPSC   0x08   /* Chip Control (reset, etc.) */

/* PLX IRQCS bits */
#define IRQCS_ENABLE_PCIIRQ  (1 << 0)
#define IRQCS_ENABLE_PCIDB   (1 << 1)

/* PLX CHIPSC bits */
#define CHIPSC_RESET_XILINX  (1L << 16)

/* Interrupt source mask bits (from driver) */
#define MASK_SYS_STATUS_CMD_DONE   (1L << 20)
#define MASK_SYS_STATUS_EOBI       (1L << 27)
#define MASK_SYS_STATUS_EOBO       (1L << 28)
#define MASK_SYS_STATUS_URUN       (1L << 30)
#define MASK_SYS_STATUS_ORUN       (1L << 29)
#define MASK_SYS_STATUS_ESA        (1L << 31)   /* guessed, see driver */
#define MASK_SYS_ASYNC_EVENTS      (MASK_SYS_STATUS_ESA | MASK_SYS_STATUS_EOBI | MASK_SYS_STATUS_EOBO | MASK_SYS_STATUS_URUN | MASK_SYS_STATUS_ORUN)

/* Error codes */
#define ED_DSP_TIMED_OUT  0x01
#define ED_DSP_CRASHED    0x02
#define ERROR_VALUE       0xFFFF0000   /* mask for high bits of error? */
#define PCX_IRQ_NONE      0x00000000

/* Maximum command/status words */
#define REG_CRM_NUMBER  12

/* Dummy MAC address for the device, preloaded to satisfy probe */
#define DUMMY_MAC_WORD1 0x78563412
#define DUMMY_MAC_WORD2 0x0000bc9a

/* -------------------------------------------------------------------- */

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status; /* interrupt status register */
    uint32_t intr_mask;   /* interrupt mask */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t dsp_cmd_regs[REG_CRM_NUMBER];  /* command mailbox */
    uint32_t dsp_stat_regs[REG_CRM_NUMBER]; /* status mailbox */

    /* DSP Control/Status */
    uint32_t dsp_csm;       /* CSM register */

    /* PLX registers */
    uint32_t plx_irqcs;     /* IRQ Control/Status */
    uint32_t plx_intrstat;  /* Interrupt Source Status (W1C) */
    uint32_t plx_chipsc;    /* Chip Control (reset) */

    /* Derived IRQ enable state */
    bool irq_enabled;

    /* Internal state for command processing */
    bool command_pending;   /* true while MR is set and not yet cleared */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int level = 0;

    if (s->irq_enabled && (s->plx_intrstat != 0)) {
        level = 1;
    }

    /* For now, always use legacy INTx; MSI not yet implemented */
    pci_set_irq(pdev, level);
}

/* -------------------------------------------------------------------- */
/* MMIO/DSP read/write handlers                                         */
/* -------------------------------------------------------------------- */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver accesses only 4-byte registers via ioread32 */
    if (size != 4) {
        return ~0ULL;
    }

    switch (addr) {
    case DSP_CRM1_BYTE_OFF ... DSP_CRM1_BYTE_OFF + REG_CRM_NUMBER*4 - 1:
        /* Fallback to dsp_cmd_regs array */
        if ((addr - DSP_CRM1_BYTE_OFF) % 4 == 0) {
            int idx = (addr - DSP_CRM1_BYTE_OFF) / 4;
            val = s->dsp_cmd_regs[idx];
        } else {
            val = 0;
        }
        break;

    case DSP_CRM2_BYTE_OFF ... DSP_CRM2_BYTE_OFF + REG_CRM_NUMBER*4 - 1:
        /* Fallback to dsp_stat_regs array */
        if ((addr - DSP_CRM2_BYTE_OFF) % 4 == 0) {
            int idx = (addr - DSP_CRM2_BYTE_OFF) / 4;
            val = s->dsp_stat_regs[idx];
        } else {
            val = 0;
        }
        break;

    case DSP_CSM_BYTE_OFF:
        val = s->dsp_csm;
        break;

    default:
        /* Unimplemented DSP register reads return 0 */
        /* TODO: Mac address registers eReg_ADMACESMSB and eReg_ADMACESLSB
         * must be implemented with actual offsets once known. */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case DSP_CRM1_BYTE_OFF ... DSP_CRM1_BYTE_OFF + REG_CRM_NUMBER*4 - 1:
        if ((addr - DSP_CRM1_BYTE_OFF) % 4 == 0) {
            int idx = (addr - DSP_CRM1_BYTE_OFF) / 4;
            s->dsp_cmd_regs[idx] = val;
        }
        break;

    case DSP_CRM2_BYTE_OFF ... DSP_CRM2_BYTE_OFF + REG_CRM_NUMBER*4 - 1:
        /* Status registers are read-only from driver perspective; ignore writes */
        break;

    case DSP_CSM_BYTE_OFF:
    {
        uint32_t new_val = val;
        /* Reflect written value except for MR, which is managed by device */
        s->dsp_csm = new_val;
        if (new_val & Reg_CSM_MC) {
            s->dsp_csm |= Reg_CSM_MR;       /* set response bit */
            s->command_pending = true;

            /* Simulate command completion: echo command with high bit set */
            s->dsp_stat_regs[0] = s->dsp_cmd_regs[0] | 0x80000000;

            s->plx_intrstat |= MASK_SYS_STATUS_CMD_DONE;
            pcibase_update_irq(s);
        } else {
            s->dsp_csm &= ~Reg_CSM_MR;      /* driver clearing MC also clears MR */
            s->command_pending = false;
        }
        break;
    }

    default:
        /* Other DSP registers silently ignored */
        break;
    }
}

/* -------------------------------------------------------------------- */
/* PIO/PLX read/write handlers                                           */
/* -------------------------------------------------------------------- */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return ~0ULL;
    }

    switch (addr) {
    case PLX_OFFSET_IRQCS:
        val = s->plx_irqcs;
        break;

    case PLX_OFFSET_INTRSTAT:
        val = s->plx_intrstat;
        break;

    case PLX_OFFSET_CHIPSC:
        val = s->plx_chipsc;
        break;

    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case PLX_OFFSET_IRQCS:
        s->plx_irqcs = val;
        s->irq_enabled = (val & IRQCS_ENABLE_PCIIRQ) != 0;
        pcibase_update_irq(s);
        break;

    case PLX_OFFSET_INTRSTAT:
        s->plx_intrstat &= ~val;   /* write 1 to clear */
        pcibase_update_irq(s);
        break;

    case PLX_OFFSET_CHIPSC:
        s->plx_chipsc = val;
        if (val & CHIPSC_RESET_XILINX) {
            /* Reset DSP (soft reset). Clear command state only;
             * do not clear status registers (MAC persists). */
            memset(s->dsp_cmd_regs, 0, sizeof(s->dsp_cmd_regs));
            s->dsp_csm = 0;
            s->command_pending = false;
            /* Do not clear plx registers; they are on a different clock */
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
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* -------------------------------------------------------------------- */
/* Reset                                                                */
/* -------------------------------------------------------------------- */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->dsp_cmd_regs, 0, sizeof(s->dsp_cmd_regs));
    /* dsp_stat_regs not cleared: MAC must persist across reset */
    s->dsp_csm = 0;
    s->plx_irqcs = 0;
    s->plx_intrstat = 0;
    s->plx_chipsc = 0;
    s->irq_enabled = false;
    s->command_pending = false;
    s->intr_status = 0;
    s->intr_mask = 0;
}

/* -------------------------------------------------------------------- */
/* BAR Registration                                                     */
/* -------------------------------------------------------------------- */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, int index,
                                 uint8_t type, hwaddr size, const char *name,
                                 Error **errp)
{
    MemoryRegion *mr = &s->bar_regions[index];
    hwaddr aligned_size = pow2ceil(size);

    if (type == PCI_BASE_ADDRESS_SPACE_MEMORY) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, name, aligned_size);
    } else if (type == PCI_BASE_ADDRESS_SPACE_IO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, name, aligned_size);
    } else {
        error_setg(errp, "Invalid BAR type");
        return;
    }
    pci_register_bar(pdev, index, type, mr);
}

/* -------------------------------------------------------------------- */
/* Realize                                                              */
/* -------------------------------------------------------------------- */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, SUBSYSTEM_VENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, SUBSYSTEM_ID);

    s->num_bars = 2;
    pcibase_register_bar(pdev, s, 1, PCI_BASE_ADDRESS_SPACE_IO, 256, "plx-bar", errp);
    pcibase_register_bar(pdev, s, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, 8192, "dsp-bar", errp);

    memset(s->dsp_cmd_regs, 0, sizeof(s->dsp_cmd_regs));
    memset(s->dsp_stat_regs, 0, sizeof(s->dsp_stat_regs));
    /* Preload a dummy non-zero MAC into status registers to pass probe */
    s->dsp_stat_regs[1] = DUMMY_MAC_WORD1;
    s->dsp_stat_regs[2] = DUMMY_MAC_WORD2;
    s->dsp_csm = 0;
    s->plx_irqcs = 0;
    s->plx_intrstat = 0;
    s->plx_chipsc = 0;
    s->irq_enabled = false;
    s->command_pending = false;
    s->intr_status = 0;
    s->intr_mask = 0;
}

/* -------------------------------------------------------------------- */
/* Uninit                                                               */
/* -------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------- */
/* VMState                                                              */
/* -------------------------------------------------------------------- */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_lx6464es_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(dsp_cmd_regs, PCIBaseState, REG_CRM_NUMBER),
        VMSTATE_UINT32_ARRAY(dsp_stat_regs, PCIBaseState, REG_CRM_NUMBER),
        VMSTATE_UINT32(dsp_csm, PCIBaseState),
        VMSTATE_UINT32(plx_irqcs, PCIBaseState),
        VMSTATE_UINT32(plx_intrstat, PCIBaseState),
        VMSTATE_UINT32(plx_chipsc, PCIBaseState),
        VMSTATE_BOOL(irq_enabled, PCIBaseState),
        VMSTATE_BOOL(command_pending, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_mask, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

/* -------------------------------------------------------------------- */
/* Class Init                                                           */
/* -------------------------------------------------------------------- */
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
