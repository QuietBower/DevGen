/*
 * QEMU 8.2.10 PCI device model for ESS ES1938 (Solo-1) audio controller.
 * Generated from Linux driver snd_es1938.c (sound/pci/es1938.c).
 * Phase 4: Debug & Update - Runtime refinement.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "snd_es1938_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/*
 * Register Layout and Hardware Identifiers extracted from driver source.
 * All offsets are literal macro definitions from es1938.c and its headers.
 */

/* PCM channel indices */
#define DAC1 0x01
#define ADC1 0x02
#define DAC2 0x04
#define SUPPORT_JOYSTICK 1

/* ESS I/O register offsets (Base 0: io_port) */
#define ESSIO_REG_AUDIO2DMAADDR   0
#define ESSIO_REG_AUDIO2DMACOUNT  1
#define ESSIO_REG_AUDIO2MODE      6
#define ESSIO_REG_IRQCONTROL      7

/* ESS DMA register offsets (Base 2: ddma_port = vc_port + 0x00) */
#define ESSDM_REG_DMAADDR    0x00
#define ESSDM_REG_DMACOUNT   0x04
#define ESSDM_REG_DMACOMMAND 0x08
#define ESSDM_REG_DMASTATUS  0x08
#define ESSDM_REG_DMAMODE    0x0b
#define ESSDM_REG_DMACLEAR   0x0d
#define ESSDM_REG_DMAMASK    0x0f

/* Sound Blaster compatible register offsets (Base 1: sb_port) */
#define ESSSB_REG_FMLOWADDR     0x00
#define ESSSB_REG_FMHIGHADDR    0x02
#define ESSSB_REG_MIXERADDR     0x04
#define ESSSB_REG_MIXERDATA     0x05
#define ESSSB_REG_RESET         0x06
#define ESSSB_REG_READDATA      0x0a
#define ESSSB_REG_WRITEDATA     0x0c
#define ESSSB_REG_READSTATUS    0x0c
#define ESSSB_REG_STATUS        0x0e

/* Indirect registers (accessed via MIXERADDR/MIXERDATA) */
#define ESSSB_IREG_AUDIO1           0x14
#define ESSSB_IREG_MICMIX           0x1a
#define ESSSB_IREG_RECSRC           0x1c
#define ESSSB_IREG_MASTER           0x32
#define ESSSB_IREG_FM               0x36
#define ESSSB_IREG_AUXACD           0x38
#define ESSSB_IREG_AUXB             0x3a
#define ESSSB_IREG_PCSPEAKER        0x3c
#define ESSSB_IREG_LINE             0x3e
#define ESSSB_IREG_SPATCONTROL      0x50
#define ESSSB_IREG_SPATLEVEL        0x52
#define ESSSB_IREG_MASTER_LEFT      0x60
#define ESSSB_IREG_MASTER_RIGHT     0x62
#define ESSSB_IREG_MPU401CONTROL    0x64
#define ESSSB_IREG_MICMIXRECORD     0x68
#define ESSSB_IREG_AUDIO2RECORD     0x69
#define ESSSB_IREG_AUXACDRECORD     0x6a
#define ESSSB_IREG_FMRECORD         0x6b
#define ESSSB_IREG_AUXBRECORD       0x6c
#define ESSSB_IREG_MONO             0x6d
#define ESSSB_IREG_LINERECORD       0x6e
#define ESSSB_IREG_MONORECORD       0x6f
#define ESSSB_IREG_AUDIO2SAMPLE     0x70
#define ESSSB_IREG_AUDIO2MODE       0x71
#define ESSSB_IREG_AUDIO2FILTER     0x72
#define ESSSB_IREG_AUDIO2TCOUNTL    0x74
#define ESSSB_IREG_AUDIO2TCOUNTH    0x76
#define ESSSB_IREG_AUDIO2CONTROL1   0x78
#define ESSSB_IREG_AUDIO2CONTROL2   0x7a
#define ESSSB_IREG_AUDIO2           0x7c

/* Extended ESS commands (sent to a command register) */
#define ESS_CMD_EXTSAMPLERATE   0xa1
#define ESS_CMD_FILTERDIV       0xa2
#define ESS_CMD_DMACNTRELOADL   0xa4
#define ESS_CMD_DMACNTRELOADH   0xa5
#define ESS_CMD_ANALOGCONTROL   0xa8
#define ESS_CMD_IRQCONTROL      0xb1
#define ESS_CMD_DRQCONTROL      0xb2
#define ESS_CMD_RECLEVEL        0xb4
#define ESS_CMD_SETFORMAT       0xb6
#define ESS_CMD_SETFORMAT2      0xb7
#define ESS_CMD_DMACONTROL      0xb8
#define ESS_CMD_DMATYPE         0xb9
#define ESS_CMD_OFFSETLEFT      0xba
#define ESS_CMD_OFFSETRIGHT     0xbb
#define ESS_CMD_READREG         0xc0
#define ESS_CMD_ENABLEEXT       0xc6
#define ESS_CMD_PAUSEDMA        0xd0
#define ESS_CMD_ENABLEAUDIO1    0xd1
#define ESS_CMD_STOPAUDIO1      0xd3
#define ESS_CMD_AUDIO1STATUS    0xd8
#define ESS_CMD_CONTDMA         0xd4
#define ESS_CMD_TESTIRQ         0xf2

/* Recording source constants */
#define ESS_RECSRC_MIC      0
#define ESS_RECSRC_AUXACD   2
#define ESS_RECSRC_AUXB     5
#define ESS_RECSRC_LINE     6
#define ESS_RECSRC_NONE     7

/* Power management saved register list */
#define SAVED_REG_SIZE 32

/* Timeout constants for reset loops (unused in QEMU, kept for reference) */
#define RESET_LOOP_TIMEOUT  0x10000
#define WRITE_LOOP_TIMEOUT  0x10000
#define GET_LOOP_TIMEOUT    0x01000

/* Convenience macros for port calculation (unused in device model, kept for documentation) */
#define SLIO_REG(chip, x) ((chip)->io_port + ESSIO_REG_##x)
#define SLDM_REG(chip, x) ((chip)->ddma_port + ESSDM_REG_##x)
#define SLSB_REG(chip, x) ((chip)->sb_port + ESSSB_REG_##x)

/*
 * BAR enumeration and sizes.
 * All BARs are PIO, sizes derived from typical ES1938 resource requirements.
 * BAR0: ESS I/O registers (max offset 0x07, but hardware uses 0x40) -> size 0x40
 * BAR1: Sound Blaster compatible registers (max 0x0E) -> size 0x10
 * BAR2: VC/ESFM/DDMA registers (max 0x0F) -> size 0x10
 * BAR3: MPU401 UART (2 ports) -> size 0x02
 * BAR4: Gameport (1 port, padded to 2 bytes for power-of-two) -> size 0x02
 */
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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* BAR0 state (ESS I/O) */
    uint32_t audio2dmaaddr;
    uint16_t audio2dmacount;
    uint8_t audio2mode;
    uint8_t irqcontrol;

    /* BAR1 state (Sound Blaster compatible) */
    uint8_t mixer_addr;
    uint8_t mixer_regs[256];
    uint8_t essregs[256];          /* Extended command register file */
    uint8_t sb_status;             /* Status at offset 0x0e */
    uint8_t sb_read_data;          /* Read data at offset 0x0a */
    uint8_t cmd_state;             /* 0=idle, 1=expect_data, 2=expect_read_reg */
    uint8_t cmd_reg;               /* Stored command or register address */
    uint8_t ext_mode;              /* Extended mode enabled flag */
    uint8_t audio1_status;         /* Audio 1 status (returned by 0xD8) */

    /* BAR2 state (DDMA) */
    uint32_t dmaaddr;
    uint16_t dmacount;
    uint8_t dmacommand;            /* Written value */
    uint8_t dma_status;            /* Returned on read */
    uint8_t dmamode;
    uint8_t dmamask;
};

/* Command processing for WRITEDATA port */
static void handle_write_cmd(PCIBaseState *s, uint8_t cmd)
{
    switch (s->cmd_state) {
    case 0: /* idle */
        switch (cmd) {
        case ESS_CMD_READREG:  /* 0xC0: read extended register */
            s->cmd_state = 2; /* expect register number */
            break;
        case ESS_CMD_AUDIO1STATUS:  /* 0xD8: return audio1 status directly */
            s->sb_read_data = s->audio1_status;
            s->sb_status |= 0x80;
            break;
        case ESS_CMD_ENABLEEXT:      /* 0xC6: enable extended mode, single byte */
            s->ext_mode = 1;
            break;
        case ESS_CMD_ENABLEAUDIO1:   /* 0xD1: enable audio1, single byte */
            s->audio1_status = 1;
            break;
        case ESS_CMD_STOPAUDIO1:     /* 0xD3: stop audio1, single byte */
            s->audio1_status = 0;
            break;
        case ESS_CMD_PAUSEDMA:       /* 0xD0: pause DMA, single byte */
            /* Ignore */
            break;
        case ESS_CMD_CONTDMA:        /* 0xD4: continue DMA, single byte */
            /* Ignore */
            break;
        case ESS_CMD_TESTIRQ:        /* 0xF2: test IRQ, single byte */
            /* Ignore */
            break;
        case 0xE1: /* Get DSP version, single byte reply (commonly used in probe) */
            s->sb_read_data = 0x01; /* Major version 1, minor 0 implied */
            s->sb_status |= 0x80;
            break;
        /* Commands that require a data byte */
        case ESS_CMD_IRQCONTROL:     /* 0xB1 */
        case ESS_CMD_DMACONTROL:     /* 0xB8 */
        case ESS_CMD_DMATYPE:        /* 0xB9 */
        case ESS_CMD_SETFORMAT:      /* 0xB6 */
        case ESS_CMD_SETFORMAT2:     /* 0xB7 */
        case ESS_CMD_EXTSAMPLERATE:  /* 0xA1 */
        case ESS_CMD_FILTERDIV:      /* 0xA2 */
        case ESS_CMD_DMACNTRELOADL:  /* 0xA4 */
        case ESS_CMD_DMACNTRELOADH:  /* 0xA5 */
        case ESS_CMD_ANALOGCONTROL:  /* 0xA8 */
        case ESS_CMD_DRQCONTROL:     /* 0xB2 */
        case ESS_CMD_RECLEVEL:       /* 0xB4 */
        case ESS_CMD_OFFSETLEFT:     /* 0xBA */
        case ESS_CMD_OFFSETRIGHT:    /* 0xBB */
            s->cmd_reg = cmd;  /* store command for later data */
            s->cmd_state = 1;  /* expect data byte */
            break;
        default:
            /* Unknown command, ignore */
            break;
        }
        break;
    case 1: /* expect data */
        /* Execute command with data */
        switch (s->cmd_reg) {
        case ESS_CMD_IRQCONTROL:
            s->irqcontrol = cmd;  /* mirror to BAR0 */
            break;
        case ESS_CMD_DMACONTROL:
        case ESS_CMD_DMATYPE:
        case ESS_CMD_SETFORMAT:
        case ESS_CMD_SETFORMAT2:
        case ESS_CMD_EXTSAMPLERATE:
        case ESS_CMD_FILTERDIV:
        case ESS_CMD_DMACNTRELOADL:
        case ESS_CMD_DMACNTRELOADH:
        case ESS_CMD_ANALOGCONTROL:
        case ESS_CMD_DRQCONTROL:
        case ESS_CMD_RECLEVEL:
        case ESS_CMD_OFFSETLEFT:
        case ESS_CMD_OFFSETRIGHT:
            /* For now, store data in a command-specific variable or just ignore */
            break;
        default:
            /* Unknown combination */
            break;
        }
        s->cmd_state = 0;  /* back to idle */
        break;
    case 2: /* expect read register */
        /* After ESS_CMD_READREG, this byte is the register index */
        s->sb_read_data = s->essregs[cmd];
        s->sb_status |= 0x80;
        s->cmd_state = 0;
        break;
    default:
        break;
    }
}

/* BAR0 PIO handlers (ESS I/O registers) */
static uint64_t pcibase_pio_read_bar0(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0:
        if (size == 4) val = s->audio2dmaaddr;
        break;
    case 1:
        if (size == 2) val = s->audio2dmacount;
        break;
    case 6:
        if (size == 1) val = s->audio2mode;
        break;
    case 7:
        if (size == 1) val = s->irqcontrol;
        break;
    default:
        val = 0xff;
        break;
    }
    return val;
}

static void pcibase_pio_write_bar0(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0:
        if (size == 4) s->audio2dmaaddr = (uint32_t)val;
        break;
    case 1:
        if (size == 2) s->audio2dmacount = (uint16_t)val;
        break;
    case 6:
        if (size == 1) s->audio2mode = (uint8_t)val;
        break;
    case 7:
        if (size == 1) s->irqcontrol = (uint8_t)val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_pio_bar0_ops = {
    .read = pcibase_pio_read_bar0,
    .write = pcibase_pio_write_bar0,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* BAR1 PIO handlers (Sound Blaster compatible) */
static uint64_t pcibase_pio_read_bar1(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;

    switch (addr) {
    case 0x00: /* FMLOWADDR */
    case 0x02: /* FMHIGHADDR */
        /* OPL3 access - ignore, return 0xff */
        break;
    case 0x04: /* MIXERADDR */
        val = s->mixer_addr;
        break;
    case 0x05: /* MIXERDATA */
        val = s->mixer_regs[s->mixer_addr];
        break;
    case 0x06: /* RESET */
        val = 0; /* Whatever was written */
        break;
    case 0x0a: /* READDATA */
        val = s->sb_read_data;
        s->sb_status &= ~0x80; /* Clear data available */
        break;
    case 0x0c: /* READSTATUS */
        val = 0; /* Always ready (bit 7 clear) */
        break;
    case 0x0e: /* STATUS */
        val = s->sb_status;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_pio_write_bar1(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x00: /* FMLOWADDR */
    case 0x02: /* FMHIGHADDR */
        /* OPL3 - ignore */
        break;
    case 0x04: /* MIXERADDR */
        s->mixer_addr = (uint8_t)val;
        break;
    case 0x05: /* MIXERDATA */
        s->mixer_regs[s->mixer_addr] = (uint8_t)val;
        break;
    case 0x06: /* RESET */
        if (val == 1) {
            /* First stage of reset: clear status */
            s->sb_status = 0;
            s->sb_read_data = 0;
        } else if (val == 0) {
            /* Second stage: reset complete, generate DSP ready signal */
            s->sb_status = 0x80;
            s->sb_read_data = 0xaa;
        }
        break;
    case 0x0c: /* WRITEDATA (command byte) */
        handle_write_cmd(s, (uint8_t)val);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_pio_bar1_ops = {
    .read = pcibase_pio_read_bar1,
    .write = pcibase_pio_write_bar1,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

/* BAR2 PIO handlers (DDMA/VC/ESFM registers) */
static uint64_t pcibase_pio_read_bar2(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;

    switch (addr) {
    case 0x00: /* DMAADDR */
        if (size == 4) val = s->dmaaddr;
        break;
    case 0x04: /* DMACOUNT */
        if (size == 2) val = s->dmacount;
        break;
    case 0x08: /* DMASTATUS (read) */
        if (size == 1) val = s->dma_status;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_pio_write_bar2(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x00: /* DMAADDR */
        if (size == 4) s->dmaaddr = (uint32_t)val;
        break;
    case 0x04: /* DMACOUNT */
        if (size == 2) s->dmacount = (uint16_t)val;
        break;
    case 0x08: /* DMACOMMAND (write) */
        if (size == 1) s->dmacommand = (uint8_t)val;
        break;
    case 0x0b: /* DMAMODE */
        if (size == 1) s->dmamode = (uint8_t)val;
        break;
    case 0x0d: /* DMACLEAR */
        /* Master reset: do nothing for now */
        break;
    case 0x0f: /* DMAMASK */
        if (size == 1) s->dmamask = (uint8_t)val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_pio_bar2_ops = {
    .read = pcibase_pio_read_bar2,
    .write = pcibase_pio_write_bar2,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* BAR3/4 PIO handlers (MPU401/Gameport) */
static uint64_t pcibase_pio_read_bar3(void *opaque, hwaddr addr, unsigned size)
{
    return 0xff;
}

static void pcibase_pio_write_bar3(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_pio_bar3_ops = {
    .read = pcibase_pio_read_bar3,
    .write = pcibase_pio_write_bar3,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl  = { .min_access_size = 1, .max_access_size = 2 },
};

static uint64_t pcibase_pio_read_bar4(void *opaque, hwaddr addr, unsigned size)
{
    return 0xff;
}

static void pcibase_pio_write_bar4(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_pio_bar4_ops = {
    .read = pcibase_pio_read_bar4,
    .write = pcibase_pio_write_bar4,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pci = PCI_DEVICE(dev);

    /* Reset base PCI device state */
    pci_device_reset(pci);

    /* Reset internal state */
    memset(&s->audio2dmaaddr, 0, sizeof(*s) - offsetof(PCIBaseState, audio2dmaaddr));
    s->cmd_state = 0;
    s->sb_status = 0;
    s->sb_read_data = 0;
    s->ext_mode = 0;
    s->audio1_status = 0;

    /* Pre-initialize indirect registers to plausible values for detection */
    /* Many drivers check for a non-zero revision ID */
    s->essregs[0x00] = 0x01;  /* chip revision or ID register */
    s->essregs[0x01] = 0x01;  /* possible revision/version register */
    s->essregs[0x40] = 0x01;  /* alternative location */

    /* Set some mixer defaults to valid states */
    for (int i = 0; i < 256; i++) {
        s->mixer_regs[i] = 0x00;  /* mute by default */
    }
    s->mixer_regs[0x14] = 0x80; /* master volume left */
    s->mixer_regs[0x32] = 0x80; /* master mono */
    s->mixer_regs[0x36] = 0x80; /* FM volume */
    s->mixer_regs[0x38] = 0x80; /* AUX A */
    s->mixer_regs[0x3a] = 0x80; /* AUX B */
    s->mixer_regs[0x3c] = 0x80; /* PC speaker */
    s->mixer_regs[0x3e] = 0x80; /* line */

    /* Initialize DMA registers */
    s->dmaaddr = 0;
    s->dmacount = 0;
    s->dmacommand = 0;
    s->dma_status = 0;
    s->dmamode = 0;
    s->dmamask = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = ROUND_UP(bi->size, 4); /* ensure power of two and alignment */
    MemoryRegion *mr = &s->bar_regions[bi->index];
    const MemoryRegionOps *ops = NULL;

    /* Select appropriate ops based on BAR index */
    switch (bi->index) {
    case 0:
        ops = &pcibase_pio_bar0_ops;
        break;
    case 1:
        ops = &pcibase_pio_bar1_ops;
        break;
    case 2:
        ops = &pcibase_pio_bar2_ops;
        break;
    case 3:
        ops = &pcibase_pio_bar3_ops;
        break;
    case 4:
        ops = &pcibase_pio_bar4_ops;
        break;
    default:
        g_assert_not_reached();
    }

    if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_MMIO) {
        /* Not used in this device */
        g_assert_not_reached();
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x125D);  /* ESS Technology */
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1969);  /* ES1938 (Solo-1) */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x125D);  /* Subsystem vendor: ESS */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID,       0x1969);  /* Subsystem device: same as device */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0401); /* Multimedia audio controller */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
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
    .name = "snd_es1938_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_instance_init(Object *obj)
{
    PCIBaseState *s = PCIBASE_DEVICE(obj);

    /* Define BAR layout matching driver resource usage */
    s->num_bars = 5;

    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 0x40, .name = "ess-io" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 0x10, .name = "ess-sb" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 0x10, .name = "ess-vc" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_PIO, .size = 0x02, .name = "ess-mpu" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_PIO, .size = 0x02, .name = "ess-game" };

    /* Initialize internal state to known values */
    memset(&s->audio2dmaaddr, 0, sizeof(*s) - offsetof(PCIBaseState, audio2dmaaddr));
}

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
        .instance_init = pcibase_instance_init,
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
