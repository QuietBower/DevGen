/*
 * QEMU Lola PCI Audio Device Emulation
 * Based on snd-lola driver analysis.
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
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

/* Lola register definitions */
#ifndef LOLA_REGISTERS_H
#define LOLA_REGISTERS_H

/* BAR0 register offsets */
#define LOLA_BAR0_GCTL       0x00
#define LOLA_BAR0_CORBLBASE  0x10
#define LOLA_BAR0_CORBUBASE  0x14
#define LOLA_BAR0_CORBWP     0x18
#define LOLA_BAR0_CORBRP     0x1C
#define LOLA_BAR0_CORBCTL    0x20
#define LOLA_BAR0_CORBSIZE   0x24
#define LOLA_BAR0_CORBSTS    0x28
#define LOLA_BAR0_RIRBLBASE  0x30
#define LOLA_BAR0_RIRBUBASE  0x34
#define LOLA_BAR0_RIRBWP     0x38
#define LOLA_BAR0_RIRBCTL    0x40
#define LOLA_BAR0_RIRBSIZE   0x44
#define LOLA_BAR0_RIRBSTS    0x48
#define LOLA_BAR0_RINTCNT    0x50

#define LOLA_BAR0_SIZE       0x100

/* BAR1 register offsets */
#define LOLA_BAR1_DINTSTS    0x00
#define LOLA_BAR1_DINTCTL    0x04
#define LOLA_BAR1_DIINTSTS   0x08
#define LOLA_BAR1_DIINTCTL   0x0C
#define LOLA_BAR1_DOINTSTS   0x10
#define LOLA_BAR1_DOINTCTL   0x14
#define LOLA_BAR1_DEVER      0x1C
#define LOLA_BAR1_BOARD_MODE 0x20
#define LOLA_BAR1_DSD_BASE   0x1000
#define LOLA_BAR1_SIZE       0x2000

#define LOLA_DSD_STRIDE      0x20

/* PCI IDs */
#define LOLA_VENDOR_ID       0x1369
#define LOLA_DEVICE_ID       0x0011
#define LOLA_CLASS_ID        0x040300

/* GCTL bits */
#define LOLA_GCTL_RESET      0x00000001

/* CORB/RIRB Control and Status bits */
#define LOLA_RBCTL_IRQ_EN    0x02
#define LOLA_CORB_INT_MASK   0x01
#define LOLA_RIRB_INT_MASK   0x01

/* Write pointer clear */
#define LOLA_RBRWP_CLR       0xFFFFFFFF

/* Interrupt status bits */
#define LOLA_DINT_GLOBAL     0x80000000
#define LOLA_DINT_CTRL       0x00000001
#define LOLA_DINT_FIFOERR    0x00000002
#define LOLA_DINT_MUERR      0x00000004

/* Verbs and parameters */
#define LOLA_VERB_GET_PARAMETER   0xF00
#define LOLA_PAR_VENDOR_ID        0x00
#define LOLA_PAR_FUNCTION_TYPE    0x01
#define LOLA_PAR_SPECIFIC_CAPS    0x12

/* Specific capabilities bit definitions */
#define LOLA_AFG_INPUT_PIN_COUNT(n)       (n)
#define LOLA_AFG_OUTPUT_PIN_COUNT(n)      (n)
#define LOLA_AFG_CLOCK_WIDGET_PRESENT_MASK  0x1000
#define LOLA_AFG_MIXER_WIDGET_PRESENT_MASK  0x2000

/* RIRB response error */
#define LOLA_RIRB_EX_ERROR    0x01

#endif /* LOLA_REGISTERS_H */

#define TYPE_PCIBASE_DEVICE "snd_lola_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Maximum number of streams based on driver constants */
#define MAX_STREAM_IN  16
#define MAX_STREAM_OUT 16

/* Per-stream descriptor structure */
typedef struct {
    uint32_t sts;
    uint32_t lpib;
    uint32_t ctl;
    uint32_t lvi;
} StreamDesc;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar0_region;
    MemoryRegion bar1_region;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    /* Interrupt state */
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    /* BAR0 - Global and Ring Buffer Registers */
    uint32_t gctl;
    uint32_t corb_lbase;
    uint32_t corb_ubase;
    uint32_t corb_wp;
    uint32_t corb_rp_shadow; /* internal read pointer */
    uint32_t corb_ctl;
    uint8_t  corb_size;
    uint8_t  corb_sts;
    uint32_t rirb_lbase;
    uint32_t rirb_ubase;
    uint32_t rirb_wp_shadow; /* internal write pointer */
    uint32_t rirb_rp;
    uint32_t rirb_ctl;
    uint8_t  rirb_size;
    uint8_t  rirb_sts;
    uint16_t rintcnt;

    /* BAR1 - Interrupt and Stream Registers */
    uint32_t dintsts;
    uint32_t dintctl;
    uint32_t diintsts;
    uint32_t diintctl;
    uint32_t dointsts;
    uint32_t dointctl;
    uint32_t dever;
    uint32_t board_mode;

    /* Stream descriptors (in and out) */
    StreamDesc in_streams[MAX_STREAM_IN];
    StreamDesc out_streams[MAX_STREAM_OUT];
    uint32_t num_in_streams;
    uint32_t num_out_streams;

    /* DMA Context */
    uint64_t corb_base;
    uint64_t rirb_base;
    uint16_t corb_entries;
    uint16_t rirb_entries;

    /* Probe Reset state */
    bool cold_reset;

    /* Timer for command processing */
    QEMUTimer *cmd_timer;
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->dintctl & LOLA_DINT_GLOBAL) {
        if (s->dintsts & s->dintctl) {
            pci_set_irq(pdev, 1);
        } else {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA: process CORB commands */
static void process_corb_commands(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint16_t wp = s->corb_wp;
    uint16_t rp = s->corb_rp_shadow;
    uint16_t entries = s->corb_entries;

    while (rp != wp) {
        uint64_t addr = s->corb_base + (rp * 8);
        uint32_t cmd[2]; /* le32 */
        uint32_t res = 0, res_ex = 0;
        uint8_t error = 0;

        if (pci_dma_read(pdev, addr, cmd, 8) != 0) {
            /* DMA error */
            error = 1;
        } else {
            uint32_t data = le32_to_cpu(cmd[0]);
            uint32_t extdata = le32_to_cpu(cmd[1]);
            unsigned int nid = (data >> 20) & 0xF;   /* typical 4-bit NID */
            unsigned int verb = (data >> 8) & 0xFFF; /* typical 12-bit verb */

            /* Process command; populate res and res_ex */
            /* TODO: handle codec commands. For now, respond with success (0) */
            /* Specific parameter responses needed for probe */
            if (nid == 0 && verb == LOLA_VERB_GET_PARAMETER) {
                /* Parameter ID is in lower bits of data (payload) */
                unsigned int param = data & 0xFF; /* simplified; may be larger */
                switch (param) {
                case LOLA_PAR_VENDOR_ID:
                    res = 0x1369 << 16; /* Vendor ID: 0x1369 */
                    break;
                case LOLA_PAR_FUNCTION_TYPE:
                    res = 1;
                    break;
                case LOLA_PAR_SPECIFIC_CAPS:
                    /* Define a reasonable speccaps that gives at least 1 pin in/out */
                    res = LOLA_AFG_INPUT_PIN_COUNT(2) | (LOLA_AFG_OUTPUT_PIN_COUNT(2) << 16) | LOLA_AFG_CLOCK_WIDGET_PRESENT_MASK | LOLA_AFG_MIXER_WIDGET_PRESENT_MASK;
                    break;
                default:
                    res = 0;
                    break;
                }
            } else {
                /* For all other verbs, just return success */
                res = 0;
                res_ex = 0;
            }
        }

        if (error) {
            res_ex |= LOLA_RIRB_EX_ERROR;
        }

        /* Write response to RIRB */
        uint64_t rirb_addr = s->rirb_base + (s->rirb_wp_shadow * 8);
        uint32_t rirb_data[2];
        rirb_data[0] = cpu_to_le32(res);
        rirb_data[1] = cpu_to_le32(res_ex);
        if (pci_dma_write(pdev, rirb_addr, rirb_data, 8) != 0) {
            /* DMA write error */
        }

        /* Update RIRB write pointer */
        s->rirb_wp_shadow = (s->rirb_wp_shadow + 1) % s->rirb_entries;
        /* Update RIRBWP register (readable by driver) */
        s->rirb_rp = s->rirb_wp_shadow; /* Actually RIRBWP is write pointer seen by driver */

        /* Set RIRB interrupt status */
        s->rirb_sts |= LOLA_RIRB_INT_MASK;
        if (s->rirb_ctl & LOLA_RBCTL_IRQ_EN) {
            s->dintsts |= LOLA_DINT_CTRL;
        }

        rp = (rp + 1) % entries;
    }
    s->corb_rp_shadow = rp;
    pcibase_update_irq(s);
}

/* BAR0 MMIO Handlers */
static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case LOLA_BAR0_GCTL:
        val = s->gctl;
        break;
    case LOLA_BAR0_CORBLBASE:
        val = s->corb_lbase;
        break;
    case LOLA_BAR0_CORBUBASE:
        val = s->corb_ubase;
        break;
    case LOLA_BAR0_CORBWP:
        val = s->corb_wp;
        break;
    case LOLA_BAR0_CORBRP:
        val = s->corb_rp_shadow;
        break;
    case LOLA_BAR0_CORBCTL:
        val = s->corb_ctl;
        break;
    case LOLA_BAR0_CORBSIZE:
        val = s->corb_size;
        break;
    case LOLA_BAR0_CORBSTS:
        val = s->corb_sts;
        break;
    case LOLA_BAR0_RIRBLBASE:
        val = s->rirb_lbase;
        break;
    case LOLA_BAR0_RIRBUBASE:
        val = s->rirb_ubase;
        break;
    case LOLA_BAR0_RIRBWP:
        val = s->rirb_wp_shadow;
        break;
    case LOLA_BAR0_RIRBCTL:
        val = s->rirb_ctl;
        break;
    case LOLA_BAR0_RIRBSIZE:
        val = s->rirb_size;
        break;
    case LOLA_BAR0_RIRBSTS:
        val = s->rirb_sts;
        break;
    case LOLA_BAR0_RINTCNT:
        val = s->rintcnt;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: BAR0 unknown read at 0x%" HWADDR_PRIx " size %d\n",
                      __func__, addr, size);
        break;
    }
    return val;
}

static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case LOLA_BAR0_GCTL:
        s->gctl = val;
        if (val & LOLA_GCTL_RESET) {
            /* Perform reset */
            s->cold_reset = 1;
            /* After reset, set GCTL to indicate ready */
            s->gctl = 0xFFFFFFFF; /* typical ready pattern */
        }
        break;
    case LOLA_BAR0_CORBLBASE:
        s->corb_lbase = val;
        s->corb_base = deposit64(s->corb_base, 0, 32, val);
        break;
    case LOLA_BAR0_CORBUBASE:
        s->corb_ubase = val;
        s->corb_base = deposit64(s->corb_base, 32, 32, val);
        break;
    case LOLA_BAR0_CORBWP:
        s->corb_wp = val;
        process_corb_commands(s);
        break;
    case LOLA_BAR0_CORBRP:
        if (val == LOLA_RBRWP_CLR) {
            s->corb_rp_shadow = 0;
        } else {
            s->corb_rp_shadow = val;
        }
        break;
    case LOLA_BAR0_CORBCTL:
        s->corb_ctl = val;
        break;
    case LOLA_BAR0_CORBSIZE:
        s->corb_size = val;
        /* Calculate entries based on size encoding: 0x02 -> 256 */
        if (val == 0x02) {
            s->corb_entries = 256;
        } else {
            s->corb_entries = 2 * (val + 1); /* fallback */
        }
        break;
    case LOLA_BAR0_CORBSTS:
        /* W1C: clear bits written as 1 */
        s->corb_sts &= ~(val & LOLA_CORB_INT_MASK);
        /* After clearing, update DINTSTS if no pending CORB interrupt */
        if (!(s->corb_sts & LOLA_CORB_INT_MASK)) {
            s->dintsts &= ~LOLA_DINT_CTRL; /* may be re-asserted later */
        }
        pcibase_update_irq(s);
        break;
    case LOLA_BAR0_RIRBLBASE:
        s->rirb_lbase = val;
        s->rirb_base = deposit64(s->rirb_base, 0, 32, val);
        break;
    case LOLA_BAR0_RIRBUBASE:
        s->rirb_ubase = val;
        s->rirb_base = deposit64(s->rirb_base, 32, 32, val);
        break;
    case LOLA_BAR0_RIRBWP:
        if (val == LOLA_RBRWP_CLR) {
            s->rirb_wp_shadow = 0;
        } else {
            s->rirb_wp_shadow = val;
        }
        break;
    case LOLA_BAR0_RIRBCTL:
        s->rirb_ctl = val;
        break;
    case LOLA_BAR0_RIRBSIZE:
        s->rirb_size = val;
        if (val == 0x02) {
            s->rirb_entries = 256;
        } else {
            s->rirb_entries = 2 * (val + 1);
        }
        break;
    case LOLA_BAR0_RIRBSTS:
        s->rirb_sts &= ~(val & LOLA_RIRB_INT_MASK);
        if (!(s->rirb_sts & LOLA_RIRB_INT_MASK)) {
            s->dintsts &= ~LOLA_DINT_CTRL;
        }
        pcibase_update_irq(s);
        break;
    case LOLA_BAR0_RINTCNT:
        s->rintcnt = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: BAR0 unknown write at 0x%" HWADDR_PRIx " size %d val 0x%" PRIx64 "\n",
                      __func__, addr, size, val);
        break;
    }
}

/* BAR1 MMIO Handlers */
static uint64_t pcibase_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= LOLA_BAR1_DSD_BASE) {
        /* Stream descriptor area */
        hwaddr offset = addr - LOLA_BAR1_DSD_BASE;
        uint32_t stream = offset / LOLA_DSD_STRIDE;
        hwaddr reg = offset % LOLA_DSD_STRIDE;
        if (stream < s->num_in_streams) {
            /* Input stream */
            StreamDesc *sd = &s->in_streams[stream];
            switch (reg) {
            case 0x00: val = sd->sts; break;
            case 0x04: val = sd->lpib; break;
            case 0x08: val = sd->ctl; break;
            case 0x0C: val = sd->lvi; break;
            }
        } else {
            stream -= s->num_in_streams;
            if (stream < s->num_out_streams) {
                StreamDesc *sd = &s->out_streams[stream];
                switch (reg) {
                case 0x00: val = sd->sts; break;
                case 0x04: val = sd->lpib; break;
                case 0x08: val = sd->ctl; break;
                case 0x0C: val = sd->lvi; break;
                }
            }
        }
    } else {
        /* Global registers */
        switch (addr) {
        case LOLA_BAR1_DINTSTS:
            val = s->dintsts;
            break;
        case LOLA_BAR1_DINTCTL:
            val = s->dintctl;
            break;
        case LOLA_BAR1_DIINTSTS:
            val = s->diintsts;
            break;
        case LOLA_BAR1_DIINTCTL:
            val = s->diintctl;
            break;
        case LOLA_BAR1_DOINTSTS:
            val = s->dointsts;
            break;
        case LOLA_BAR1_DOINTCTL:
            val = s->dointctl;
            break;
        case LOLA_BAR1_DEVER:
            val = s->dever;
            break;
        case LOLA_BAR1_BOARD_MODE:
            val = s->board_mode;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: BAR1 unknown read at 0x%" HWADDR_PRIx " size %d\n",
                          __func__, addr, size);
            break;
        }
    }
    return val;
}

static void pcibase_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= LOLA_BAR1_DSD_BASE) {
        hwaddr offset = addr - LOLA_BAR1_DSD_BASE;
        uint32_t stream = offset / LOLA_DSD_STRIDE;
        hwaddr reg = offset % LOLA_DSD_STRIDE;
        StreamDesc *sd = NULL;
        if (stream < s->num_in_streams) {
            sd = &s->in_streams[stream];
        } else {
            stream -= s->num_in_streams;
            if (stream < s->num_out_streams) {
                sd = &s->out_streams[stream];
            }
        }
        if (sd) {
            switch (reg) {
            case 0x00: sd->sts = val; break;
            case 0x04: sd->lpib = val; break;
            case 0x08: sd->ctl = val; break;
            case 0x0C: sd->lvi = val; break;
            }
        }
    } else {
        switch (addr) {
        case LOLA_BAR1_DINTSTS:
            /* W1C for interrupt status bits */
            s->dintsts &= ~(val & (LOLA_DINT_CTRL | LOLA_DINT_FIFOERR | LOLA_DINT_MUERR));
            pcibase_update_irq(s);
            break;
        case LOLA_BAR1_DINTCTL:
            s->dintctl = val;
            pcibase_update_irq(s);
            break;
        case LOLA_BAR1_DIINTSTS:
            s->diintsts = val; /* maybe W1C? We'll assume direct write */
            break;
        case LOLA_BAR1_DIINTCTL:
            s->diintctl = val;
            break;
        case LOLA_BAR1_DOINTSTS:
            s->dointsts = val;
            break;
        case LOLA_BAR1_DOINTCTL:
            s->dointctl = val;
            break;
        case LOLA_BAR1_DEVER:
            /* Read-only, ignore write */
            break;
        case LOLA_BAR1_BOARD_MODE:
            s->board_mode = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: BAR1 unknown write at 0x%" HWADDR_PRIx " size %d val 0x%" PRIx64 "\n",
                          __func__, addr, size, val);
            break;
        }
    }
}

static const MemoryRegionOps pcibase_bar0_mmio_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_bar1_mmio_ops = {
    .read = pcibase_bar1_read,
    .write = pcibase_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Stream descriptor VMState */
static const VMStateDescription vmstate_stream_desc = {
    .name = "stream_desc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(sts, StreamDesc),
        VMSTATE_UINT32(lpib, StreamDesc),
        VMSTATE_UINT32(ctl, StreamDesc),
        VMSTATE_UINT32(lvi, StreamDesc),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all registers to power-on defaults */
    s->gctl = 0;
    s->corb_lbase = 0;
    s->corb_ubase = 0;
    s->corb_wp = 0;
    s->corb_rp_shadow = 0;
    s->corb_ctl = 0;
    s->corb_size = 0;
    s->corb_sts = 0;
    s->corb_base = 0;
    s->corb_entries = 0;

    s->rirb_lbase = 0;
    s->rirb_ubase = 0;
    s->rirb_wp_shadow = 0;
    s->rirb_rp = 0;
    s->rirb_ctl = 0;
    s->rirb_size = 0;
    s->rirb_sts = 0;
    s->rirb_base = 0;
    s->rirb_entries = 0;
    s->rintcnt = 0;

    s->dintsts = 0;
    s->dintctl = 0;
    s->diintsts = 0;
    s->diintctl = 0;
    s->dointsts = 0;
    s->dointctl = 0;
    s->dever = 0x01000802; /* 2 in, 2 out, version 1 */
    s->board_mode = 0;

    s->num_in_streams = 2;
    s->num_out_streams = 2;
    memset(s->in_streams, 0, sizeof(s->in_streams));
    memset(s->out_streams, 0, sizeof(s->out_streams));

    s->cold_reset = false;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, LOLA_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, LOLA_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, LOLA_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR0: Global and ring buffer registers */
    hwaddr bar0_size = pow2ceil(LOLA_BAR0_SIZE);
    memory_region_init_io(&s->bar0_region, OBJECT(s), &pcibase_bar0_mmio_ops, s,
                          "lola-bar0", bar0_size);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0_region);

    /* BAR1 (resource index 2): Stream and interrupt registers */
    hwaddr bar1_size = pow2ceil(LOLA_BAR1_SIZE);
    memory_region_init_io(&s->bar1_region, OBJECT(s), &pcibase_bar1_mmio_ops, s,
                          "lola-bar1", bar1_size);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar1_region);

    /* DMA does not need special init beyond base pointer storage */
    /* No additional DMA configuration */
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
    /* No additional uninit needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_lola_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(gctl, PCIBaseState),
        VMSTATE_UINT32(corb_lbase, PCIBaseState),
        VMSTATE_UINT32(corb_ubase, PCIBaseState),
        VMSTATE_UINT32(corb_wp, PCIBaseState),
        VMSTATE_UINT32(corb_rp_shadow, PCIBaseState),
        VMSTATE_UINT32(corb_ctl, PCIBaseState),
        VMSTATE_UINT8(corb_size, PCIBaseState),
        VMSTATE_UINT8(corb_sts, PCIBaseState),
        VMSTATE_UINT32(rirb_lbase, PCIBaseState),
        VMSTATE_UINT32(rirb_ubase, PCIBaseState),
        VMSTATE_UINT32(rirb_wp_shadow, PCIBaseState),
        VMSTATE_UINT32(rirb_rp, PCIBaseState),
        VMSTATE_UINT32(rirb_ctl, PCIBaseState),
        VMSTATE_UINT8(rirb_size, PCIBaseState),
        VMSTATE_UINT8(rirb_sts, PCIBaseState),
        VMSTATE_UINT16(rintcnt, PCIBaseState),
        VMSTATE_UINT32(dintsts, PCIBaseState),
        VMSTATE_UINT32(dintctl, PCIBaseState),
        VMSTATE_UINT32(diintsts, PCIBaseState),
        VMSTATE_UINT32(diintctl, PCIBaseState),
        VMSTATE_UINT32(dointsts, PCIBaseState),
        VMSTATE_UINT32(dointctl, PCIBaseState),
        VMSTATE_UINT32(dever, PCIBaseState),
        VMSTATE_UINT32(board_mode, PCIBaseState),
        VMSTATE_UINT32(num_in_streams, PCIBaseState),
        VMSTATE_UINT32(num_out_streams, PCIBaseState),
        VMSTATE_STRUCT_ARRAY(in_streams, PCIBaseState, MAX_STREAM_IN, 0,
                             vmstate_stream_desc, StreamDesc),
        VMSTATE_STRUCT_ARRAY(out_streams, PCIBaseState, MAX_STREAM_OUT, 0,
                             vmstate_stream_desc, StreamDesc),
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
