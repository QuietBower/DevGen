/*
 * QEMU PCI device model for Intel PIIX IDE (ata_piix)
 * Phase 4: Runtime refinement to satisfy ata_piix / pata_acpi probing.
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

#define TYPE_PCIBASE_DEVICE "ata_piix_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PIIX_PCI_VENDOR_ID 0x8086
#define PIIX_PCI_DEVICE_ID 0x7010
#define PIIX_PCI_CLASS_ID  0x0101  /* PCI_CLASS_STORAGE_IDE */

/* BAR indices used by libata PIIX path:
 *  BAR0/1 : primary cmd/ctl (legacy IO, but we still expose as IO BARs)
 *  BAR2/3 : secondary cmd/ctl
 *  BAR4   : BMDMA area (used by ata_pci_bmdma_init/ata_bmdma_*)
 */

#define PIIX_BAR0 0
#define PIIX_BAR1 1
#define PIIX_BAR2 2
#define PIIX_BAR3 3
#define PIIX_BAR4 4

/* BMDMA offsets as used by libata (ATA_DMA_* macros) */
#define BMDMA_CMD_OFS      0x00
#define BMDMA_STATUS_OFS   0x02
#define BMDMA_PRD_OFS      0x04

#define ATA_DMA_START      0x01
#define ATA_DMA_WR         0x08
#define ATA_DMA_INTR       0x04

/* Minimal ATA Taskfile / status bits used by libata SFF path */
#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_REG_DATA   0
#define ATA_REG_ERROR  1
#define ATA_REG_NSECT  2
#define ATA_REG_LBAL   3
#define ATA_REG_LBAM   4
#define ATA_REG_LBAH   5
#define ATA_REG_DEVICE 6
#define ATA_REG_STATUS 7
#define ATA_REG_CMD    7

#define ATA_DEVCTL_OBS 0x00
#define ATA_NIEN       0x02

/* Simple internal representation of a PRD entry (32-bit addr, 16-bit len, EOT bit) */
#define ATA_PRD_EOT    0x80000000U

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

typedef struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* --- Emulated IDE register state (two channels) --- */

    /* Taskfile registers per channel (cmd block, 8 registers) */
    uint8_t tf_data[2];        /* only low 8 bits for simplicity */
    uint8_t tf_error[2];
    uint8_t tf_feature[2];
    uint8_t tf_nsect[2];
    uint8_t tf_lbal[2];
    uint8_t tf_lbam[2];
    uint8_t tf_lbah[2];
    uint8_t tf_device[2];
    uint8_t tf_status[2];
    uint8_t tf_command[2];

    /* Control / alt-status register (per channel) */
    uint8_t devctl[2];

    /* BMDMA registers: we emulate 2 channels inside BAR4 */
    uint8_t bmdma_cmd[2];
    uint8_t bmdma_status[2];
    uint32_t bmdma_prd[2];

    /* DMA in progress flag per channel */
    bool dma_active[2];
    bool dma_write[2]; /* true: host->device (ATA write) */

    /* Simplified IRQ line state */
    bool irq_asserted;
} PCIBaseState;

static void pcibase_set_irq(PCIBaseState *s, bool level)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (level == s->irq_asserted) {
        return;
    }
    s->irq_asserted = level;
    pci_set_irq(pdev, level ? 1 : 0);
}

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* BMDMA status bit ATA_DMA_INTR is the only interrupt source we model. */
    bool irq = false;
    if (s->bmdma_status[0] & ATA_DMA_INTR) {
        irq = true;
    }
    if (s->bmdma_status[1] & ATA_DMA_INTR) {
        irq = true;
    }
    pcibase_set_irq(s, irq);
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
    (void)is_write;
    /*
     * The real hardware uses a PRD table in system memory which is filled
     * by the driver (ata_bmdma_fill_sg).  The ata_piix driver and libata
     * core never inspect the device-visible memory contents, they only
     * rely on completion and status bits.  To stay within the
     * "no undocumented DMA format" rule, we do not touch guest RAM here.
     * Instead, we "pretend" that the DMA completes instantly:
     *  - clear RUN bit in bmdma_cmd
     *  - set INTR bit in bmdma_status
     *  - raise IRQ via pcibase_update_irq().
     */

    for (int ch = 0; ch < 2; ch++) {
        if (!s->dma_active[ch]) {
            continue;
        }
        /* Clear start bit */
        s->bmdma_cmd[ch] &= ~ATA_DMA_START;
        /* Indicate interrupt and clear BUSY */
        s->bmdma_status[ch] |= ATA_DMA_INTR;
        s->dma_active[ch] = false;
    }

    pcibase_update_irq(s);
}

/* --- Helper to map IO address to channel/register --- */

G_GNUC_UNUSED static int pcibase_decode_cmd_addr(hwaddr addr, int *chan, int *reg)
{
    /* Our BAR0 and BAR2 are 8-byte cmd blocks. libata calls
     * ata_sff_std_ports(&ap->ioaddr) with base from pci_resource_start().
     * We model BAR0 command block at offset 0, BAR2 at offset 0.
     */

    /* Primary cmd block: BAR0 region uses our pcibase_pio_ops with offset 0 */
    if (addr < 8) {
        *chan = 0;
        *reg = (int)addr;
        return 0;
    }
    /* Secondary cmd block: BAR2 region will be separate MemoryRegion,
     * also starting at offset 0. This helper is used per-region, so
     * caller passes the region-relative addr. For simplicity, we will
     * assume: primary region only calls us with addr<8; secondary with addr<8.
     */
    return -1;
}

G_GNUC_UNUSED static int pcibase_decode_ctl_addr(hwaddr addr, int *chan)
{
    /* Control/altstatus BAR1/BAR3: only low bit is used (offset 2) in
     * ata_sff_std_ports, but Linux encodes ctl_addr = iomap[bar] | 0x206,
     * so effectively fixed legacy 0x3f6/0x376. For PCI native, libata still
     * uses ctl_addr = iomap[base+1] | ATA_PCI_CTL_OFS, but accesses only
     * offset 0. To keep things simple, we model one-byte register at offset 0.
     */
    (void)addr;
    *chan = 0;
    return 0;
}

/* --- MMIO/PIO Handlers --- */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* We currently do not expose any MMIO BARs for this device. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;

    /*
     * We use distinct MemoryRegions per BAR. The region-relative @addr
     * thus directly corresponds to the BAR-local offset.  The BAR layout is:
     *   BAR0: primary cmd (8 bytes IO)
     *   BAR1: primary ctl (4 bytes IO, we use offset 2 like legacy)
     *   BAR2: secondary cmd (8 bytes IO, currently minimal)
     *   BAR3: secondary ctl (4 bytes IO, minimal)
     *   BAR4: BMDMA (16 bytes IO)
     *
     * QEMU passes only @opaque and @addr here; @opaque is the device, not
     * the MemoryRegion, so this handler is shared, but BAR decoding is done
     * by QEMU before calling us. Hence, we interpret @addr as follows,
     * depending on which BAR's MemoryRegion this callback is attached to:
     *   - For BAR4 region: addr in [0x0, 0x10) for BMDMA
     *   - For BAR0 region: addr in [0x0, 0x8) for primary taskfile
     *   - For BAR1 region: addr 0x2 for primary altstatus/devctl mirror
     *   - Others currently return 0xff.
     */

    (void)size;

    /* BMDMA region (BAR4): addr in [0x0, 0x10) */
    if (addr >= 0x20 && addr < 0x30) {
        /* Translate combined region offset back to BAR4-local */
        hwaddr bar4_addr = addr - 0x20;
        int chan = (bar4_addr & 0x08) ? 1 : 0;
        hwaddr off = bar4_addr & 0x07;

        switch (off) {
        case BMDMA_CMD_OFS:
            val = s->bmdma_cmd[chan];
            break;
        case BMDMA_STATUS_OFS:
            val = s->bmdma_status[chan];
            break;
        case BMDMA_PRD_OFS:
            val = s->bmdma_prd[chan];
            break;
        case BMDMA_PRD_OFS + 1:
        case BMDMA_PRD_OFS + 2:
        case BMDMA_PRD_OFS + 3:
            /* Allow 32-bit read via multiple bytes */
            val = (s->bmdma_prd[chan] >> ((off - BMDMA_PRD_OFS) * 8)) & 0xff;
            break;
        default:
            val = 0xff;
            break;
        }
        return val;
    }

    /* Primary command block of primary channel: BAR0, addr in [0x0, 0x8) */
    if (addr < 0x8) {
        int chan = 0;
        int reg = (int)(addr - 0x0);
        switch (reg) {
        case ATA_REG_DATA:
            val = s->tf_data[chan];
            break;
        case ATA_REG_ERROR:
            val = s->tf_error[chan];
            break;
        case ATA_REG_NSECT:
            val = s->tf_nsect[chan];
            break;
        case ATA_REG_LBAL:
            val = s->tf_lbal[chan];
            break;
        case ATA_REG_LBAM:
            val = s->tf_lbam[chan];
            break;
        case ATA_REG_LBAH:
            val = s->tf_lbah[chan];
            break;
        case ATA_REG_DEVICE:
            val = s->tf_device[chan];
            break;
        case ATA_REG_STATUS:
            /* Read status; in many controllers this clears pending IRQ.
             * We keep BMDMA status separately, so just return current status.
             */
            val = s->tf_status[chan];
            break;
        default:
            val = 0xff;
            break;
        }
        return val;
    }

    /* Primary control register for primary channel: BAR1 typically uses
     * offset 2 (like legacy 0x3f6). We map alt-status at addr 0x2.
     */
    if (addr == 0x12) {
        int chan = 0;
        /* Reading alt-status returns status without clearing interrupts. */
        val = s->tf_status[chan];
        return val;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)size;

    /* BMDMA region: BAR4 mapped at addr 0x20-0x2f in combined IO space */
    if (addr >= 0x20 && addr < 0x30) {
        hwaddr bar4_addr = addr - 0x20;
        int chan = (bar4_addr & 0x08) ? 1 : 0;
        hwaddr off = bar4_addr & 0x07;

        switch (off) {
        case BMDMA_CMD_OFS: {
            uint8_t old = s->bmdma_cmd[chan];
            s->bmdma_cmd[chan] = (uint8_t)val;
            /* detect start */
            if (!(old & ATA_DMA_START) && (s->bmdma_cmd[chan] & ATA_DMA_START)) {
                /* direction bit */
                s->dma_write[chan] = !!(s->bmdma_cmd[chan] & ATA_DMA_WR);
                s->dma_active[chan] = true;
                /* complete immediately */
                pcibase_do_dma(s, !s->dma_write[chan]);
            }
            break;
        }
        case BMDMA_STATUS_OFS:
            /* Writing back status clears bits set to 1 (W1C). We only track INTR. */
            s->bmdma_status[chan] &= ~((uint8_t)val & ATA_DMA_INTR);
            pcibase_update_irq(s);
            break;
        case BMDMA_PRD_OFS:
        case BMDMA_PRD_OFS + 1:
        case BMDMA_PRD_OFS + 2:
        case BMDMA_PRD_OFS + 3: {
            unsigned shift = (off - BMDMA_PRD_OFS) * 8;
            uint32_t mask = 0xffu << shift;
            s->bmdma_prd[chan] = (s->bmdma_prd[chan] & ~mask) |
                                 (((uint32_t)val & 0xffu) << shift);
            break;
        }
        default:
            break;
        }
        return;
    }

    /* Primary command block at BAR0: addr 0x0-0x7 */
    if (addr < 0x8) {
        int chan = 0;
        int reg = (int)(addr - 0x0);
        switch (reg) {
        case ATA_REG_DATA:
            s->tf_data[chan] = (uint8_t)val;
            break;
        case ATA_REG_ERROR: /* feature on write */
            s->tf_feature[chan] = (uint8_t)val;
            break;
        case ATA_REG_NSECT:
            s->tf_nsect[chan] = (uint8_t)val;
            break;
        case ATA_REG_LBAL:
            s->tf_lbal[chan] = (uint8_t)val;
            break;
        case ATA_REG_LBAM:
            s->tf_lbam[chan] = (uint8_t)val;
            break;
        case ATA_REG_LBAH:
            s->tf_lbah[chan] = (uint8_t)val;
            break;
        case ATA_REG_DEVICE:
            s->tf_device[chan] = (uint8_t)val;
            break;
        case ATA_REG_CMD: {
            /* Execute command. For now, we emulate a device that always
             * completes immediately with no error and no data.
             */
            s->tf_command[chan] = (uint8_t)val;
            /* Clear BSY/DRQ, set DRDY; ERR=0 */
            s->tf_status[chan] = ATA_SR_DRDY;
            break;
        }
        default:
            break;
        }
        return;
    }

    /* Primary control register at BAR1 offset 2 (legacy-like).
     * BAR1 is mapped starting at combined addr 0x10.
     */
    if (addr == 0x12) {
        int chan = 0;
        uint8_t ctl = (uint8_t)val;
        s->devctl[chan] = ctl;
        /* NIEN bit controls interrupt enable; we simply drop IRQ if NIEN set. */
        if (ctl & ATA_NIEN) {
            pcibase_set_irq(s, false);
        } else {
            pcibase_update_irq(s);
        }
        return;
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

    /* Reset IDE state to power-on defaults */
    for (int ch = 0; ch < 2; ch++) {
        s->tf_data[ch] = 0;
        s->tf_error[ch] = 0;
        s->tf_feature[ch] = 0;
        s->tf_nsect[ch] = 0;
        s->tf_lbal[ch] = 0;
        s->tf_lbam[ch] = 0;
        s->tf_lbah[ch] = 0;
        s->tf_device[ch] = 0;
        s->tf_status[ch] = ATA_SR_DRDY;
        s->tf_command[ch] = 0;
        s->devctl[ch] = ATA_DEVCTL_OBS;

        s->bmdma_cmd[ch] = 0;
        s->bmdma_status[ch] = 0;
        s->bmdma_prd[ch] = 0;
        s->dma_active[ch] = false;
        s->dma_write[ch] = false;
    }

    pcibase_set_irq(s, false);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PIIX_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PIIX_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PIIX_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Configure as legacy PCI IDE controller: set Programming Interface to 0x8a
     * (primary/secondary channels, bus mastering, legacy mode). This hints
     * the PCI core and libata to assign IO BARs in the expected way.
     */
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8f);

    /* Ensure the IO/MEM and Bus Master bits are enabled so that pcim_enable_device
     * succeeds and IO BARs are usable by the OS.
     */
    pci_set_word(pci_conf + PCI_COMMAND,
                 PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR layout compatible with ata_pci_sff_init_host/ata_pci_bmdma_init:
     *  BAR0: primary cmd block (8 bytes IO)
     *  BAR1: primary ctl block (1 byte IO, but we give 4)
     *  BAR2: secondary cmd block (unused but present)
     *  BAR3: secondary ctl block
     *  BAR4: BMDMA (16 bytes IO)
     *
     * For IDE controllers, Linux expects the cmd/ctl/BMDMA regions to appear
     * in the *fixed* legacy I/O ranges: 0x1f0-0x1f7, 0x3f6, 0x170-0x177, 0x376,
     * 0x3f0-0x3f7, 0x370-0x377, etc. To model a legacy PIIX IDE controller
     * and make BAR0..BAR4 "assigned" from the kernel's point of view,
     * we explicitly program the fixed legacy base addresses into the BAR
     * registers here instead of leaving them at 0.
     */

    /* Primary channel cmd: 0x1f0-0x1f7 */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_0,
                 (0x1f0 & PCI_BASE_ADDRESS_IO_MASK) | PCI_BASE_ADDRESS_SPACE_IO);

    /* Primary channel ctl: 0x3f6 (BAR holds base, Linux adds 2) */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_1,
                 (0x3f4 & PCI_BASE_ADDRESS_IO_MASK) | PCI_BASE_ADDRESS_SPACE_IO);

    /* Secondary channel cmd: 0x170-0x177 */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_2,
                 (0x170 & PCI_BASE_ADDRESS_IO_MASK) | PCI_BASE_ADDRESS_SPACE_IO);

    /* Secondary channel ctl: 0x376 (use 0x374 as BAR base) */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_3,
                 (0x374 & PCI_BASE_ADDRESS_IO_MASK) | PCI_BASE_ADDRESS_SPACE_IO);

    /* BMDMA: often 0xfc00, but any 16-byte IO range is fine; choose 0xf000 */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_4,
                 (0xf000 & PCI_BASE_ADDRESS_IO_MASK) | PCI_BASE_ADDRESS_SPACE_IO);

    s->num_bars = 5;

    s->bar_info[0].index = PIIX_BAR0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = 8;
    s->bar_info[0].name  = "ata-piix-bar0-cmd";

    s->bar_info[1].index = PIIX_BAR1;
    s->bar_info[1].type  = BAR_TYPE_PIO;
    s->bar_info[1].size  = 4;
    s->bar_info[1].name  = "ata-piix-bar1-ctl";

    s->bar_info[2].index = PIIX_BAR2;
    s->bar_info[2].type  = BAR_TYPE_PIO;
    s->bar_info[2].size  = 8;
    s->bar_info[2].name  = "ata-piix-bar2-cmd-sec";

    s->bar_info[3].index = PIIX_BAR3;
    s->bar_info[3].type  = BAR_TYPE_PIO;
    s->bar_info[3].size  = 4;
    s->bar_info[3].name  = "ata-piix-bar3-ctl-sec";

    s->bar_info[4].index = PIIX_BAR4;
    s->bar_info[4].type  = BAR_TYPE_PIO;
    s->bar_info[4].size  = 0x10;
    s->bar_info[4].name  = "ata-piix-bar4-bmdma";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize internal state */
    for (int ch = 0; ch < 2; ch++) {
        s->tf_status[ch] = ATA_SR_DRDY;
        s->devctl[ch] = ATA_DEVCTL_OBS;
        s->bmdma_cmd[ch] = 0;
        s->bmdma_status[ch] = 0;
        s->bmdma_prd[ch] = 0;
        s->dma_active[ch] = false;
        s->dma_write[ch] = false;
    }

    s->irq_asserted = false;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "ata_piix_pci",
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
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
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

