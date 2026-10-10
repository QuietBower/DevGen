/*
 * QEMU PCI device model skeleton for cx88-mpeg (cx8802) driver
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "cx88_mpeg_driver_manager_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CX8802_VENDOR_ID 0x14f1
#define CX8802_DEVICE_ID 0x8802

/* Selected register offsets and bit definitions used by cx88-mpeg driver */
#define MO_PCI_INTMSK       0x200040
#define MO_PCI_INTSTAT      0x200044
#define MO_PDMA_STHRSH      0x200000
#define MO_PDMA_DTHRSH      0x200010
#define MO_TS_INTMSK        0x200070
#define MO_TS_INTSTAT       0x200074
#define MO_VID_INTMSK       0x200050
#define MO_AUD_INTMSK       0x200060
#define MO_VIP_INTMSK       0x200080
#define MO_GPHST_INTMSK     0x200090
#define MO_TS_GPCNT         0x33C020
#define MO_TS_GPCNTRL       0x33C030
#define MO_TS_DMACNTRL      0x33C040
#define MO_TS_LNGTH         0x33C048
#define TS_GEN_CNTRL        0x33C050
#define TS_SOP_STAT         0x33C058
#define TS_VALERR_CNTRL     0x33C060
#define MO_DEV_CNTRL2       0x200034
#define MO_PINMUX_IO        0x35C044
#define MO_SRST_IO          0x35C05C
#define MO_SAMPLE_IO        0x35C058
#define MO_INT1_STAT        0x35C064
#define MO_GP0_IO           0x350010
#define MO_GP1_IO           0x350014
#define MO_GP2_IO           0x350018
#define MO_I2C              0x368000
#define VID_CAPTURE_CONTROL 0x310180
#define MO_COLOR_CTRL       0x310184
#define MO_AGC_BACK_VBI     0x310200
#define MO_AGC_SYNC_TIP1    0x310208
#define MO_INPUT_FORMAT     0x310104

/* PCI interrupt bits */
#define PCI_INT_TSINT            (1 << 2)
#define PCI_INT_RISC_WR_BERRINT  (1 << 11)
#define PCI_INT_RISC_RD_BERRINT  (1 << 10)
#define PCI_INT_BRDG_BERRINT     (1 << 12)
#define PCI_INT_SRC_DMA_BERRINT  (1 << 13)
#define PCI_INT_DST_DMA_BERRINT  (1 << 14)
#define PCI_INT_IPB_DMA_BERRINT  (1 << 15)
#define PCI_INT_IR_SMPINT        (1 << 18)

/* RISC engine command bits (not interpreted by this model) */
#define RISC_JUMP       0x70000000
#define RISC_IRQ1       0x01000000
#define RISC_CNT_INC    0x00010000
#define RISC_SOL        0x08000000
#define RISC_RESYNC     0x80008000
#define RISC_WRITE      0x10000000
#define RISC_EOL        0x04000000
#define RISC_READ       0x90000000
#define RISC_READC      0xA0000000
#define RISC_WRITECM    0xC0000000
#define RISC_WRITEC     0x50000000
#define RISC_WRITERM    0xB0000000
#define RISC_SKIP       0x20000000
#define RISC_SYNC       0x80000000
#define RISC_WRITECR    0xD0000000

/* Misc */
#define GP_COUNT_CONTROL_RESET 0x3

/* Simplified TS INT bits used by the driver */
#define TS_INT_RISC1_Y      (1u << 0)   /* status & 0x01 */
#define TS_INT_RISC_OP_ERR  (1u << 16)  /* status & (1 << 16) */
#define TS_INT_GEN_ERR_MASK 0x1f0100u   /* status & 0x1f0100 */

/* TS DMA control bits relevant to emulation */
#define TS_DMACNTRL_ENABLE  0x01u

/* MO_TS_DMACNTRL bit 0x10 also set/cleared by driver; keep mask used there */
#define TS_DMACNTRL_MASK    0x11u

/* MO_DEV_CNTRL2 bit 5 used to start RISC controller in driver */
#define DEV_CNTRL2_RISC_ENABLE (1u << 5)

/* Maximum number of packets to simulate per wake-up when DMA is running */
#define TS_SIM_PACKETS_PER_IRQ 1

/* Simple generator for TS_GPCNT increment */
#define TS_GPCNT_INCREMENT 1


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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t mo_pci_intmsk;
        uint32_t mo_pci_intstat;
        uint32_t mo_ts_intmsk;
        uint32_t mo_ts_intstat;
        uint32_t mo_ts_gpcnt;
        uint32_t mo_ts_gpcntrl;
        uint32_t mo_ts_dmacntrl;
        uint32_t mo_ts_lngth;
        uint32_t ts_gen_cntrl;
        uint32_t ts_sop_stat;
        uint32_t ts_valerr_cntrl;
        uint32_t mo_dev_cntrl2;
        uint32_t mo_pinmux_io;
        uint32_t mo_srst_io;
        uint32_t mo_sample_io;
        uint32_t mo_int1_stat;
        uint32_t mo_gp0_io;
        uint32_t mo_gp1_io;
        uint32_t mo_gp2_io;
        uint32_t mo_i2c;
        uint32_t vid_capture_control;
        uint32_t mo_color_ctrl;
        uint32_t mo_agc_back_vbi;
        uint32_t mo_agc_sync_tip1;
        uint32_t mo_input_format;
        uint32_t mo_vid_intmsk;
        uint32_t mo_aud_intmsk;
        uint32_t mo_vip_intmsk;
        uint32_t mo_gphst_intmsk;
    } regs;

    /* Simple model of running TS DMA */
    bool ts_dma_running;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending;

    /* In the real hardware, PCI interrupt is asserted when various
     * MO_PCI_INTSTAT bits are set and unmasked by MO_PCI_INTMSK.
     * The driver always uses level-triggered shared IRQ and clears
     * bits with write-1-to-clear semantics.
     */
    pending = s->regs.mo_pci_intstat & s->regs.mo_pci_intmsk;

    if (pending) {
        /* No MSI/MSI-X for this model, just use INTx */
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns.
 * The Linux driver programs on-card SRAM and a RISC engine. The
 * actual TS payload is passed to upper layers via vb2; this QEMU
 * model does not need to move real TS data, but it must emulate
 * the side effects (TS_GPCNT increments and interrupts).
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
    /* No actual bus-mastering transfers are required for driver probe
     * and basic operation; the driver never inspects TS memory via MMIO.
     */
}

/* Helper to update TS interrupt/PCI interrupt when TS status changes */
static void pcibase_update_ts_irq(PCIBaseState *s)
{
    uint32_t ts_pending = s->regs.mo_ts_intstat & s->regs.mo_ts_intmsk;

    if (ts_pending) {
        /* Signal TSINT via PCI INT status */
        s->regs.mo_pci_intstat |= PCI_INT_TSINT;
    } else {
        s->regs.mo_pci_intstat &= ~PCI_INT_TSINT;
    }

    pcibase_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver only uses 32-bit accesses through cx_read/cx_write.
     * For other sizes, behave conservatively and return 0.
     */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case MO_PCI_INTMSK:
        val = s->regs.mo_pci_intmsk;
        break;
    case MO_PCI_INTSTAT:
        /* Read-only snapshot; write-1-to-clear handled in write path */
        val = s->regs.mo_pci_intstat;
        break;
    case MO_TS_INTMSK:
        val = s->regs.mo_ts_intmsk;
        break;
    case MO_TS_INTSTAT:
        val = s->regs.mo_ts_intstat;
        break;
    case MO_TS_GPCNT:
        val = s->regs.mo_ts_gpcnt;
        break;
    case MO_TS_GPCNTRL:
        val = s->regs.mo_ts_gpcntrl;
        break;
    case MO_TS_DMACNTRL:
        val = s->regs.mo_ts_dmacntrl;
        break;
    case MO_TS_LNGTH:
        val = s->regs.mo_ts_lngth;
        break;
    case TS_GEN_CNTRL:
        val = s->regs.ts_gen_cntrl;
        break;
    case TS_SOP_STAT:
        val = s->regs.ts_sop_stat;
        break;
    case TS_VALERR_CNTRL:
        val = s->regs.ts_valerr_cntrl;
        break;
    case MO_DEV_CNTRL2:
        val = s->regs.mo_dev_cntrl2;
        break;
    case MO_PINMUX_IO:
        val = s->regs.mo_pinmux_io;
        break;
    case MO_SRST_IO:
        val = s->regs.mo_srst_io;
        break;
    case MO_SAMPLE_IO:
        val = s->regs.mo_sample_io;
        break;
    case MO_INT1_STAT:
        val = s->regs.mo_int1_stat;
        break;
    case MO_GP0_IO:
        val = s->regs.mo_gp0_io;
        break;
    case MO_GP1_IO:
        val = s->regs.mo_gp1_io;
        break;
    case MO_GP2_IO:
        val = s->regs.mo_gp2_io;
        break;
    case MO_I2C:
        val = s->regs.mo_i2c;
        break;
    case VID_CAPTURE_CONTROL:
        val = s->regs.vid_capture_control;
        break;
    case MO_COLOR_CTRL:
        val = s->regs.mo_color_ctrl;
        break;
    case MO_AGC_BACK_VBI:
        val = s->regs.mo_agc_back_vbi;
        break;
    case MO_AGC_SYNC_TIP1:
        val = s->regs.mo_agc_sync_tip1;
        break;
    case MO_INPUT_FORMAT:
        val = s->regs.mo_input_format;
        break;
    default:
        /* For other addresses used by cx88 core (e.g., SRAM cmds), we don't
         * model detailed contents; return 0 to avoid confusing the driver.
         */
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
    case MO_PCI_INTMSK:
        /* Interrupt mask: simple read/write register */
        s->regs.mo_pci_intmsk = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case MO_PCI_INTSTAT:
        /* Write-1-to-clear semantics for PCI INT status bits */
        s->regs.mo_pci_intstat &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case MO_TS_INTMSK:
        s->regs.mo_ts_intmsk = (uint32_t)val;
        pcibase_update_ts_irq(s);
        break;
    case MO_TS_INTSTAT:
        /* TS INTSTAT is cleared by writing back the bits that were read */
        s->regs.mo_ts_intstat &= ~((uint32_t)val);
        pcibase_update_ts_irq(s);
        break;
    case MO_TS_GPCNTRL:
        s->regs.mo_ts_gpcntrl = (uint32_t)val;
        /* Driver writes GP_COUNT_CONTROL_RESET (0x3) to reset counter */
        if ((val & GP_COUNT_CONTROL_RESET) == GP_COUNT_CONTROL_RESET) {
            s->regs.mo_ts_gpcnt = 0;
        }
        break;
    case MO_TS_GPCNT:
        /* Not normally written by the driver, but keep register writable */
        s->regs.mo_ts_gpcnt = (uint32_t)val;
        break;
    case MO_TS_DMACNTRL: {
        uint32_t old = s->regs.mo_ts_dmacntrl;
        uint32_t newv = (uint32_t)val;
        s->regs.mo_ts_dmacntrl = newv;

        /* Track simple "DMA running" state based on enable bit(s). The
         * driver sets 0x11 to start and clears 0x11 to stop.
         */
        if (!(old & TS_DMACNTRL_MASK) && (newv & TS_DMACNTRL_MASK)) {
            s->ts_dma_running = true;

            /* On start, simulate one packet completion: increment
             * MO_TS_GPCNT and raise TS interrupt bit 0 if enabled.
             */
            s->regs.mo_ts_gpcnt += TS_GPCNT_INCREMENT;
            s->regs.mo_ts_intstat |= TS_INT_RISC1_Y;
            pcibase_update_ts_irq(s);
        } else if ((old & TS_DMACNTRL_MASK) && !(newv & TS_DMACNTRL_MASK)) {
            s->ts_dma_running = false;
        }
        break; }
    case MO_TS_LNGTH:
        s->regs.mo_ts_lngth = (uint32_t)val;
        break;
    case TS_GEN_CNTRL:
        s->regs.ts_gen_cntrl = (uint32_t)val;
        break;
    case TS_SOP_STAT:
        s->regs.ts_sop_stat = (uint32_t)val;
        break;
    case TS_VALERR_CNTRL:
        s->regs.ts_valerr_cntrl = (uint32_t)val;
        break;
    case MO_DEV_CNTRL2: {
        uint32_t old = s->regs.mo_dev_cntrl2;
        uint32_t newv = (uint32_t)val;
        s->regs.mo_dev_cntrl2 = newv;
        /* When the driver sets bit 5 to start the RISC controller, we
         * could choose to simulate additional DMA activity. For now we
         * just track the bit without side effects beyond TS DMA start
         * already modeled via MO_TS_DMACNTRL.
         */
        (void)old;
        break; }
    case MO_PINMUX_IO:
        s->regs.mo_pinmux_io = (uint32_t)val;
        break;
    case MO_SRST_IO:
        s->regs.mo_srst_io = (uint32_t)val;
        break;
    case MO_SAMPLE_IO:
        s->regs.mo_sample_io = (uint32_t)val;
        break;
    case MO_INT1_STAT:
        /* RISC interrupt status; driver clears with 0xFFFFFFFF. Use
         * write-1-to-clear behavior.
         */
        s->regs.mo_int1_stat &= ~((uint32_t)val);
        break;
    case MO_GP0_IO:
        s->regs.mo_gp0_io = (uint32_t)val;
        break;
    case MO_GP1_IO:
        s->regs.mo_gp1_io = (uint32_t)val;
        break;
    case MO_GP2_IO:
        s->regs.mo_gp2_io = (uint32_t)val;
        break;
    case MO_I2C:
        /* Simple I2C bit-bang register: maintain last written state so
         * that cx8800_bit_getscl/getsda can read back the value.
         */
        s->regs.mo_i2c = (uint32_t)val;
        break;
    case VID_CAPTURE_CONTROL:
        s->regs.vid_capture_control = (uint32_t)val;
        break;
    case MO_COLOR_CTRL:
        s->regs.mo_color_ctrl = (uint32_t)val;
        break;
    case MO_AGC_BACK_VBI:
        s->regs.mo_agc_back_vbi = (uint32_t)val;
        break;
    case MO_AGC_SYNC_TIP1:
        s->regs.mo_agc_sync_tip1 = (uint32_t)val;
        break;
    case MO_INPUT_FORMAT:
        s->regs.mo_input_format = (uint32_t)val;
        break;
    case MO_PDMA_STHRSH:
        /* FIFO threshold, simple RW */
        /* No dedicated field in regs; driver only writes. Ignore safely. */
        break;
    case MO_PDMA_DTHRSH:
        /* FIFO threshold, simple RW; ignore in model. */
        break;
    case MO_VID_INTMSK:
        s->regs.mo_vid_intmsk = (uint32_t)val; /* Note: field not defined; keep for completeness */
        break;
    case MO_AUD_INTMSK:
        s->regs.mo_aud_intmsk = (uint32_t)val; /* Not explicitly used by cx88-mpeg.c, but written in shutdown */
        break;
    case MO_VIP_INTMSK:
        s->regs.mo_vip_intmsk = (uint32_t)val; /* same note */
        break;
    case MO_GPHST_INTMSK:
        s->regs.mo_gphst_intmsk = (uint32_t)val; /* same note */
        break;
    default:
        /* Many other registers are accessed by cx88 core. We keep them as
         * write-only shadow locations where necessary;
         * unknown addresses are just ignored.
         */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    /* The cx88-mpeg driver does not use IO-port accesses. */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* No PIO usage by driver. */
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

    /* Revert registers to power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->ts_dma_running = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CX8802_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CX8802_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_OTHER );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 1 * MiB;
    s->bar_info[0].name  = "cx88-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X usage is visible in cx88-mpeg driver; leave disabled. */
    s->has_msi = false;
    s->has_msix = false;

    /* Final state initialization before the device is 'live' */
    memset(&s->regs, 0, sizeof(s->regs));
    s->ts_dma_running = false;
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cx88_mpeg_driver_manager_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_BOOL(ts_dma_running, PCIBaseState),
        VMSTATE_UINT32(regs.mo_pci_intmsk, PCIBaseState),
        VMSTATE_UINT32(regs.mo_pci_intstat, PCIBaseState),
        VMSTATE_UINT32(regs.mo_ts_intmsk, PCIBaseState),
        VMSTATE_UINT32(regs.mo_ts_intstat, PCIBaseState),
        VMSTATE_UINT32(regs.mo_ts_gpcnt, PCIBaseState),
        VMSTATE_UINT32(regs.mo_ts_gpcntrl, PCIBaseState),
        VMSTATE_UINT32(regs.mo_ts_dmacntrl, PCIBaseState),
        VMSTATE_UINT32(regs.mo_ts_lngth, PCIBaseState),
        VMSTATE_UINT32(regs.ts_gen_cntrl, PCIBaseState),
        VMSTATE_UINT32(regs.ts_sop_stat, PCIBaseState),
        VMSTATE_UINT32(regs.ts_valerr_cntrl, PCIBaseState),
        VMSTATE_UINT32(regs.mo_dev_cntrl2, PCIBaseState),
        VMSTATE_UINT32(regs.mo_pinmux_io, PCIBaseState),
        VMSTATE_UINT32(regs.mo_srst_io, PCIBaseState),
        VMSTATE_UINT32(regs.mo_sample_io, PCIBaseState),
        VMSTATE_UINT32(regs.mo_int1_stat, PCIBaseState),
        VMSTATE_UINT32(regs.mo_gp0_io, PCIBaseState),
        VMSTATE_UINT32(regs.mo_gp1_io, PCIBaseState),
        VMSTATE_UINT32(regs.mo_gp2_io, PCIBaseState),
        VMSTATE_UINT32(regs.mo_i2c, PCIBaseState),
        VMSTATE_UINT32(regs.vid_capture_control, PCIBaseState),
        VMSTATE_UINT32(regs.mo_color_ctrl, PCIBaseState),
        VMSTATE_UINT32(regs.mo_agc_back_vbi, PCIBaseState),
        VMSTATE_UINT32(regs.mo_agc_sync_tip1, PCIBaseState),
        VMSTATE_UINT32(regs.mo_input_format, PCIBaseState),
        VMSTATE_UINT32(regs.mo_vid_intmsk, PCIBaseState),
        VMSTATE_UINT32(regs.mo_aud_intmsk, PCIBaseState),
        VMSTATE_UINT32(regs.mo_vip_intmsk, PCIBaseState),
        VMSTATE_UINT32(regs.mo_gphst_intmsk, PCIBaseState),
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

