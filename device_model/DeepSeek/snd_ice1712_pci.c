/*
 * QEMU model for ICEnsemble ICE1712 (Envy24) audio controller.
 * Based on Linux driver ice1712.c.
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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

/*
 * PCI IDs and template constants.
 */
#define TYPE_PCIBASE_DEVICE "snd_ice1712_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x1412
#define DEVICE_ID 0x1712
#define CLASS_ID  0x0401

/*
 * Direct port offsets for BAR0 (main I/O space).
 * These are based on the standard Envy24 register map.
 */
#define ICE1712_REG_CONTROL       0x00  /* 8-bit */
#define ICE1712_REG_IRQSTAT       0x01  /* 8-bit, write-1-to-clear */
#define ICE1712_REG_IRQMASK       0x02  /* 8-bit */
#define ICE1712_REG_I2C_DEV_ADDR  0x04  /* 8-bit */
#define ICE1712_REG_I2C_CTRL      0x05  /* 8-bit */
#define ICE1712_REG_I2C_DATA      0x06  /* 8-bit */
#define ICE1712_REG_I2C_BYTE_ADDR 0x07  /* 8-bit */
#define ICE1712_REG_MPU1_CTRL     0x08  /* 8-bit */
#define ICE1712_REG_MPU2_CTRL     0x09  /* 8-bit */
#define ICE1712_REG_AC97_CMD      0x0A  /* 8-bit */
#define ICE1712_REG_AC97_INDEX    0x0B  /* 8-bit */
#define ICE1712_REG_AC97_DATA     0x0C  /* 16-bit */
#define ICE1712_REG_INDEX         0x0E  /* 8-bit, indirect index */
#define ICE1712_REG_DATA          0x0F  /* 8-bit, indirect data */
#define ICE1712_REG_CONCAP_ADDR   0x10  /* 32-bit */
#define ICE1712_REG_CONCAP_COUNT  0x14  /* 16-bit */

/* BAR1 offsets (consumer DMA / DS engine) */
#define ICE1712_DDMA_ADDR         0x00  /* 32-bit */
#define ICE1712_DDMA_COUNT        0x04  /* 16-bit */
#define ICE1712_DDMA_MODE         0x0B  /* 8-bit */
#define ICE1712_DDMA_UNKNOWN15    0x0F  /* 8-bit, written with 0 */
#define ICE1712_DDMA_INDEX        0x08  /* 8-bit, indirect DS index */
#define ICE1712_DDMA_DATA         0x0C  /* 32-bit, indirect DS data */
#define ICE1712_DDMA_INTSTAT      0x10  /* 16-bit */
#define ICE1712_DDMA_INTMASK      0x12  /* 16-bit */

/* BAR3 offsets (professional multi-track) */
#define ICE1712_MT_RATE           0x00  /* 8-bit */
#define ICE1712_MT_PLAYBACK_CONTROL 0x04  /* 32-bit */
#define ICE1712_MT_PLAYBACK_ADDR  0x08  /* 32-bit */
#define ICE1712_MT_PLAYBACK_SIZE  0x0C  /* 16-bit */
#define ICE1712_MT_PLAYBACK_COUNT 0x0E  /* 16-bit */
#define ICE1712_MT_CAPTURE_ADDR   0x10  /* 32-bit */
#define ICE1712_MT_CAPTURE_SIZE   0x14  /* 16-bit */
#define ICE1712_MT_CAPTURE_COUNT  0x16  /* 16-bit */
#define ICE1712_MT_MONITOR_ROUTECTRL 0x18  /* 8-bit */
#define ICE1712_MT_ROUTE_PSDOUT03 0x1A  /* 16-bit */
#define ICE1712_MT_ROUTE_CAPTURE  0x1C  /* 32-bit */
#define ICE1712_MT_ROUTE_SPDOUT   0x20  /* 16-bit */
#define ICE1712_MT_MONITOR_INDEX  0x22  /* 8-bit */
#define ICE1712_MT_MONITOR_VOLUME 0x24  /* 16-bit */
#define ICE1712_MT_MONITOR_RATE   0x26  /* 8-bit */
#define ICE1712_MT_MONITOR_PEAKINDEX 0x28  /* 8-bit */
#define ICE1712_MT_MONITOR_PEAKDATA  0x29  /* 8-bit */
#define ICE1712_MT_IRQ            0x2A  /* 8-bit */

/* EEPROM layout indices */
#define ICE_EEP1_CODEC    0
#define ICE_EEP1_ACLINK   1
#define ICE_EEP1_I2SID    2
#define ICE_EEP1_SPDIF    3
#define ICE_EEP1_GPIO_MASK 4
#define ICE_EEP1_GPIO_STATE 5
#define ICE_EEP1_GPIO_DIR  6

/* Bit definitions for various registers (from driver) */
#define ICE1712_SPDIF_MASTER     0x10
#define ICE1712_AC97_WRITE       0x20
#define ICE1712_AC97_READY       0x08
#define ICE1712_AC97_READ        0x10
#define ICE1712_AC97_PBK_VSR     0x02
#define ICE1712_AC97_CAP_VSR     0x01
#define ICE1712_ROUTE_AC97       0x01
#define ICE1712_RESET            0x80
#define ICE1712_NATIVE           0x01
#define ICE1712_AC97_WARM        0x40
#define ICE1712_I2C_BUSY         0x01
#define ICE1712_I2C_WRITE        0x01
#define ICE1712_I2C_EEPROM       0x80
#define ICE_I2C_EEPROM_ADDR      0xA0
#define ICE1712_CFG_PRO_I2S      0x80
#define ICE1712_CFG_NO_CON_AC97  0x10
#define ICE1712_CFG_2xMPU401     0x80  /* ??? guess; needed for driver logic */
#define ICE1712_SUBDEVICE_STDSP24 0x12141217
#define ICE1712_STDSP24_CLOCK_BIT (1<<5)

/* IRQ bits */
#define ICE1712_IRQ_MPU1   0x80
#define ICE1712_IRQ_TIMER  0x40
#define ICE1712_IRQ_MPU2   0x20
#define ICE1712_IRQ_PROPCM 0x10
#define ICE1712_IRQ_FM     0x08
#define ICE1712_IRQ_PBKDS  0x04
#define ICE1712_IRQ_CONCAP 0x02
#define ICE1712_IRQ_CONPBK 0x01

/* Multi-track IRQ bits */
#define ICE1712_MULTI_PBKSTATUS  0x01
#define ICE1712_MULTI_CAPSTATUS  0x02
#define ICE1712_MULTI_PLAYBACK   0x40
#define ICE1712_MULTI_CAPTURE    0x80

/* DMA control bits */
#define ICE1712_DMA_MODE_WRITE   0x48
#define ICE1712_DMA_AUTOINIT     0x10

/* Indirect register file indices (consumer) */
#define ICE1712_IREG_GPIO_DIRECTION    0x22
#define ICE1712_IREG_GPIO_WRITE_MASK   0x21
#define ICE1712_IREG_GPIO_DATA         0x20
#define ICE1712_IREG_PBK_CTRL          0x02
#define ICE1712_IREG_PBK_COUNT_LO      0x00
#define ICE1712_IREG_PBK_COUNT_HI      0x01
#define ICE1712_IREG_PBK_RATE_LO       0x06
#define ICE1712_IREG_PBK_RATE_MID      0x07
#define ICE1712_IREG_PBK_RATE_HI       0x08
#define ICE1712_IREG_PBK_LEFT          0x03
#define ICE1712_IREG_PBK_RIGHT         0x04
#define ICE1712_IREG_CAP_CTRL          0x12
#define ICE1712_IREG_CAP_COUNT_LO      0x10
#define ICE1712_IREG_CAP_COUNT_HI      0x11
#define ICE1712_IREG_PRO_POWERDOWN     0x31
#define ICE1712_IREG_CONSUMER_POWERDOWN 0x30

/* DS engine indirect register indices */
#define ICE1712_DSC_ADDR0      0x00
#define ICE1712_DSC_COUNT0     0x01
#define ICE1712_DSC_ADDR1      0x02
#define ICE1712_DSC_COUNT1     0x03
#define ICE1712_DSC_CONTROL    0x04
#define ICE1712_DSC_RATE       0x05
#define ICE1712_DSC_VOLUME     0x06

/* BAR types */
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

/* Opaque data used by per-BAR read/write handlers. */
typedef struct {
    PCIBaseState *dev;
    int bar;
} PCIBaseBarOpaque;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
    PCIBaseBarOpaque bar_opaques[6];

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Shadow registers for main I/O (BAR0) */
    uint8_t  control;
    uint8_t  irqstat;
    uint8_t  irqmask;
    uint8_t  i2c_dev_addr;
    uint8_t  i2c_ctrl;
    uint8_t  i2c_data;
    uint8_t  i2c_byte_addr;
    uint8_t  mpu1_ctrl;
    uint8_t  mpu2_ctrl;
    uint8_t  ac97_cmd;
    uint8_t  ac97_index;
    uint16_t ac97_data;
    uint8_t  ireg_index;
    uint8_t  ireg_data;
    uint32_t concap_addr;
    uint16_t concap_count;

    /* Indirect register file (consumer) */
    uint8_t iregs[0x40];

    /* Shadow registers for DS engine (BAR1) */
    uint32_t ddma_addr;
    uint16_t ddma_count;
    uint8_t  ddma_mode;
    uint8_t  ddma_unknown15;
    uint8_t  ddma_ds_index;
    uint32_t ddma_ds_data;
    uint16_t ddma_intstat;
    uint16_t ddma_intmask;
    /* DS indirect register file */
    uint32_t ds_regs[0x20];

    /* Shadow registers for multi-track (BAR3) */
    uint8_t  mt_rate;
    uint32_t mt_playback_control;
    uint32_t mt_playback_addr;
    uint16_t mt_playback_size;
    uint16_t mt_playback_count;
    uint32_t mt_capture_addr;
    uint16_t mt_capture_size;
    uint16_t mt_capture_count;
    uint8_t  mt_monitor_routectrl;
    uint16_t mt_route_psdout03;
    uint32_t mt_route_capture;
    uint16_t mt_route_spdout;
    uint8_t  mt_monitor_index;
    uint16_t mt_monitor_volume;
    uint8_t  mt_monitor_rate;
    uint8_t  mt_monitor_peakindex;
    uint8_t  mt_monitor_peakdata;
    uint8_t  mt_irq;

    /* EEPROM contents */
    uint8_t eeprom[32];
    /* I2C internal state */
    uint8_t i2c_current_byte;

    /* DMA placeholder */
    dma_addr_t playback_addr;
    dma_addr_t capture_addr;

    /* State flags */
    bool reset_active;
    uint8_t power_state;
};

/*
 * EEPROM helper: compute I2C byte for a given address.
 * The EEPROM is 32 bytes, address is 0-31.
 */
static uint8_t ice1712_eeprom_read_byte(PCIBaseState *s, uint8_t addr)
{
    if (addr < sizeof(s->eeprom)) {
        return s->eeprom[addr];
    }
    return 0xff;
}

/* Static EEPROM image for a default card (ST Audio DSP24). */
static const uint8_t default_eeprom[32] = {
    0x17, 0x12, 0x14, 0x12,  /* subvendor = 0x12141217 (little-endian) */
    0x20,                     /* size = 32 */
    0x01,                     /* version = 1 */
    /* data bytes (6..31) */
    0x3a,  /* ICE_EEP1_CODEC: codec configuration */
    0x10,  /* ICE_EEP1_ACLINK */
    0x00,  /* ICE_EEP1_I2SID */
    0x00,  /* ICE_EEP1_SPDIF */
    0xc0,  /* ICE_EEP1_GPIO_MASK */
    0x20,  /* ICE_EEP1_GPIO_STATE: set clock bit */
    0xff,  /* ICE_EEP1_GPIO_DIR: all output */
    /* remainder zero */
};

/* ---------- per-BAR PIO read/write implementations ---------- */

/* BAR0 (main) read */
static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseBarOpaque *op = opaque;
    PCIBaseState *s = op->dev;
    uint64_t val = ~0ULL;

    switch (addr) {
    case ICE1712_REG_CONTROL:
        if (size == 1) val = s->control;
        break;
    case ICE1712_REG_IRQSTAT:
        if (size == 1) val = s->irqstat;
        break;
    case ICE1712_REG_IRQMASK:
        if (size == 1) val = s->irqmask;
        break;
    case ICE1712_REG_I2C_DEV_ADDR:
        if (size == 1) val = s->i2c_dev_addr;
        break;
    case ICE1712_REG_I2C_CTRL:
        if (size == 1) {
            /* I2C busy is cleared once byte is ready; we always return not busy */
            val = s->i2c_ctrl & ~ICE1712_I2C_BUSY;
        }
        break;
    case ICE1712_REG_I2C_DATA:
        if (size == 1) {
            /* If a read was requested, return the requested byte */
            if (!(s->i2c_dev_addr & ICE1712_I2C_WRITE)) {
                val = s->i2c_current_byte;
            } else {
                val = s->i2c_data;
            }
        }
        break;
    case ICE1712_REG_I2C_BYTE_ADDR:
        if (size == 1) val = s->i2c_byte_addr;
        break;
    case ICE1712_REG_MPU1_CTRL:
        if (size == 1) val = s->mpu1_ctrl;
        break;
    case ICE1712_REG_MPU2_CTRL:
        if (size == 1) val = s->mpu2_ctrl;
        break;
    case ICE1712_REG_AC97_CMD:
        if (size == 1) {
            /* AC97 ready is always set; no pending command */
            s->ac97_cmd &= ~(ICE1712_AC97_WRITE | ICE1712_AC97_READ);
            s->ac97_cmd |= ICE1712_AC97_READY;
            val = s->ac97_cmd;
        }
        break;
    case ICE1712_REG_AC97_INDEX:
        if (size == 1) val = s->ac97_index;
        break;
    case ICE1712_REG_AC97_DATA:
        if (size == 2) val = s->ac97_data;
        break;
    case ICE1712_REG_INDEX:
        if (size == 1) val = s->ireg_index;
        break;
    case ICE1712_REG_DATA:
        if (size == 1) val = s->ireg_data;
        break;
    case ICE1712_REG_CONCAP_ADDR:
        if (size == 4) val = s->concap_addr;
        break;
    case ICE1712_REG_CONCAP_COUNT:
        if (size == 2) val = s->concap_count;
        break;
    default:
        break;
    }
    return val;
}

/* BAR0 (main) write */
static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseBarOpaque *op = opaque;
    PCIBaseState *s = op->dev;

    switch (addr) {
    case ICE1712_REG_CONTROL:
        if (size == 1) s->control = val;
        break;
    case ICE1712_REG_IRQSTAT:
        if (size == 1) {
            /* Write-1-to-clear: clear bits that are set in val */
            s->irqstat &= ~val;
        }
        break;
    case ICE1712_REG_IRQMASK:
        if (size == 1) s->irqmask = val;
        break;
    case ICE1712_REG_I2C_DEV_ADDR:
        if (size == 1) {
            s->i2c_dev_addr = val;
            /* If this is a read (clear WRITE bit), prepare the byte */
            if (!(val & ICE1712_I2C_WRITE)) {
                uint8_t i2c_addr = s->i2c_byte_addr;
                if ((s->i2c_dev_addr & 0xFE) == ICE_I2C_EEPROM_ADDR) {
                    s->i2c_current_byte = ice1712_eeprom_read_byte(s, i2c_addr);
                } else {
                    s->i2c_current_byte = 0xff;
                }
                /* I2C busy flag cleared (already cleared in read I2C_CTRL) */
            }
        }
        break;
    case ICE1712_REG_I2C_CTRL:
        if (size == 1) s->i2c_ctrl = val;
        break;
    case ICE1712_REG_I2C_DATA:
        if (size == 1) s->i2c_data = val;
        break;
    case ICE1712_REG_I2C_BYTE_ADDR:
        if (size == 1) s->i2c_byte_addr = val;
        break;
    case ICE1712_REG_MPU1_CTRL:
        if (size == 1) s->mpu1_ctrl = val;
        break;
    case ICE1712_REG_MPU2_CTRL:
        if (size == 1) s->mpu2_ctrl = val;
        break;
    case ICE1712_REG_AC97_CMD:
        if (size == 1) {
            s->ac97_cmd = val;
            /* If a write or read is requested, we immediately mark it complete */
            if (val & ICE1712_AC97_WRITE) {
                /* Store AC97 register write: we just accept it */
                /* The actual AC97 codec emulation is not needed for probe */
            } else if (val & ICE1712_AC97_READ) {
                /* Prepare AC97 DATA with some default value */
                s->ac97_data = 0x8000; /* dummy */
            }
            /* Always set READY and clear pending bits */
            s->ac97_cmd &= ~(ICE1712_AC97_WRITE | ICE1712_AC97_READ);
            s->ac97_cmd |= ICE1712_AC97_READY;
        }
        break;
    case ICE1712_REG_AC97_INDEX:
        if (size == 1) s->ac97_index = val;
        break;
    case ICE1712_REG_AC97_DATA:
        if (size == 2) s->ac97_data = val;
        break;
    case ICE1712_REG_INDEX:
        if (size == 1) s->ireg_index = val;
        break;
    case ICE1712_REG_DATA:
        if (size == 1) {
            s->ireg_data = val;
            /* Indirect register write */
            if (s->ireg_index < sizeof(s->iregs)) {
                s->iregs[s->ireg_index] = val;
            }
        }
        break;
    case ICE1712_REG_CONCAP_ADDR:
        if (size == 4) s->concap_addr = val;
        break;
    case ICE1712_REG_CONCAP_COUNT:
        if (size == 2) s->concap_count = val;
        break;
    default:
        break;
    }
}

/* BAR1 (ddma) read */
static uint64_t pcibase_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseBarOpaque *op = opaque;
    PCIBaseState *s = op->dev;
    uint64_t val = ~0ULL;

    switch (addr) {
    case ICE1712_DDMA_ADDR:
        if (size == 4) val = s->ddma_addr;
        break;
    case ICE1712_DDMA_COUNT:
        if (size == 2) val = s->ddma_count;
        break;
    case ICE1712_DDMA_MODE:
        if (size == 1) val = s->ddma_mode;
        break;
    case ICE1712_DDMA_UNKNOWN15:
        if (size == 1) val = 0;
        break;
    case ICE1712_DDMA_INDEX:
        if (size == 1) val = s->ddma_ds_index;
        break;
    case ICE1712_DDMA_DATA:
        if (size == 4) val = s->ddma_ds_data;
        break;
    case ICE1712_DDMA_INTSTAT:
        if (size == 2) val = s->ddma_intstat;
        break;
    case ICE1712_DDMA_INTMASK:
        if (size == 2) val = s->ddma_intmask;
        break;
    default:
        break;
    }
    return val;
}

/* BAR1 (ddma) write */
static void pcibase_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseBarOpaque *op = opaque;
    PCIBaseState *s = op->dev;

    switch (addr) {
    case ICE1712_DDMA_ADDR:
        if (size == 4) s->ddma_addr = val;
        break;
    case ICE1712_DDMA_COUNT:
        if (size == 2) s->ddma_count = val;
        break;
    case ICE1712_DDMA_MODE:
        if (size == 1) s->ddma_mode = val;
        break;
    case ICE1712_DDMA_UNKNOWN15:
        if (size == 1) /* ignored */ break;
    case ICE1712_DDMA_INDEX:
        if (size == 1) s->ddma_ds_index = val;
        break;
    case ICE1712_DDMA_DATA:
        if (size == 4) {
            s->ddma_ds_data = val;
            /* DS indirect register write */
            uint8_t chn_addr = s->ddma_ds_index;
            uint8_t channel = (chn_addr >> 4) & 0x0F;
            uint8_t reg = chn_addr & 0x0F;
            if (channel < 6 && reg < 0x20) {
                s->ds_regs[channel * 0x20 + reg] = val;
            }
        }
        break;
    case ICE1712_DDMA_INTSTAT:
        if (size == 2) s->ddma_intstat = val;
        break;
    case ICE1712_DDMA_INTMASK:
        if (size == 2) s->ddma_intmask = val;
        break;
    default:
        break;
    }
}

/* BAR2 (dmapath) read - unused registers, return 0 */
static uint64_t pcibase_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* ignored */
}

/* BAR3 (profi) read */
static uint64_t pcibase_bar3_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseBarOpaque *op = opaque;
    PCIBaseState *s = op->dev;
    uint64_t val = ~0ULL;

    switch (addr) {
    case ICE1712_MT_RATE:
        if (size == 1) val = s->mt_rate;
        break;
    case ICE1712_MT_PLAYBACK_CONTROL:
        if (size == 4) val = s->mt_playback_control;
        break;
    case ICE1712_MT_PLAYBACK_ADDR:
        if (size == 4) val = s->mt_playback_addr;
        break;
    case ICE1712_MT_PLAYBACK_SIZE:
        if (size == 2) val = s->mt_playback_size;
        break;
    case ICE1712_MT_PLAYBACK_COUNT:
        if (size == 2) val = s->mt_playback_count;
        break;
    case ICE1712_MT_CAPTURE_ADDR:
        if (size == 4) val = s->mt_capture_addr;
        break;
    case ICE1712_MT_CAPTURE_SIZE:
        if (size == 2) val = s->mt_capture_size;
        break;
    case ICE1712_MT_CAPTURE_COUNT:
        if (size == 2) val = s->mt_capture_count;
        break;
    case ICE1712_MT_MONITOR_ROUTECTRL:
        if (size == 1) val = s->mt_monitor_routectrl;
        break;
    case ICE1712_MT_ROUTE_PSDOUT03:
        if (size == 2) val = s->mt_route_psdout03;
        break;
    case ICE1712_MT_ROUTE_CAPTURE:
        if (size == 4) val = s->mt_route_capture;
        break;
    case ICE1712_MT_ROUTE_SPDOUT:
        if (size == 2) val = s->mt_route_spdout;
        break;
    case ICE1712_MT_MONITOR_INDEX:
        if (size == 1) val = s->mt_monitor_index;
        break;
    case ICE1712_MT_MONITOR_VOLUME:
        if (size == 2) val = s->mt_monitor_volume;
        break;
    case ICE1712_MT_MONITOR_RATE:
        if (size == 1) val = s->mt_monitor_rate;
        break;
    case ICE1712_MT_MONITOR_PEAKINDEX:
        if (size == 1) val = s->mt_monitor_peakindex;
        break;
    case ICE1712_MT_MONITOR_PEAKDATA:
        if (size == 1) val = s->mt_monitor_peakdata;
        break;
    case ICE1712_MT_IRQ:
        if (size == 1) val = s->mt_irq;
        break;
    default:
        break;
    }
    return val;
}

/* BAR3 (profi) write */
static void pcibase_bar3_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseBarOpaque *op = opaque;
    PCIBaseState *s = op->dev;

    switch (addr) {
    case ICE1712_MT_RATE:
        if (size == 1) s->mt_rate = val;
        break;
    case ICE1712_MT_PLAYBACK_CONTROL:
        if (size == 4) s->mt_playback_control = val;
        break;
    case ICE1712_MT_PLAYBACK_ADDR:
        if (size == 4) s->mt_playback_addr = val;
        break;
    case ICE1712_MT_PLAYBACK_SIZE:
        if (size == 2) s->mt_playback_size = val;
        break;
    case ICE1712_MT_PLAYBACK_COUNT:
        if (size == 2) s->mt_playback_count = val;
        break;
    case ICE1712_MT_CAPTURE_ADDR:
        if (size == 4) s->mt_capture_addr = val;
        break;
    case ICE1712_MT_CAPTURE_SIZE:
        if (size == 2) s->mt_capture_size = val;
        break;
    case ICE1712_MT_CAPTURE_COUNT:
        if (size == 2) s->mt_capture_count = val;
        break;
    case ICE1712_MT_MONITOR_ROUTECTRL:
        if (size == 1) s->mt_monitor_routectrl = val;
        break;
    case ICE1712_MT_ROUTE_PSDOUT03:
        if (size == 2) s->mt_route_psdout03 = val;
        break;
    case ICE1712_MT_ROUTE_CAPTURE:
        if (size == 4) s->mt_route_capture = val;
        break;
    case ICE1712_MT_ROUTE_SPDOUT:
        if (size == 2) s->mt_route_spdout = val;
        break;
    case ICE1712_MT_MONITOR_INDEX:
        if (size == 1) s->mt_monitor_index = val;
        break;
    case ICE1712_MT_MONITOR_VOLUME:
        if (size == 2) s->mt_monitor_volume = val;
        break;
    case ICE1712_MT_MONITOR_RATE:
        if (size == 1) s->mt_monitor_rate = val;
        break;
    case ICE1712_MT_MONITOR_PEAKINDEX:
        if (size == 1) s->mt_monitor_peakindex = val;
        break;
    case ICE1712_MT_MONITOR_PEAKDATA:
        if (size == 1) s->mt_monitor_peakdata = val;
        break;
    case ICE1712_MT_IRQ:
        if (size == 1) s->mt_irq = val;
        break;
    default:
        break;
    }
}

/* MemoryRegionOps for each BAR */
static const MemoryRegionOps bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps bar1_ops = {
    .read = pcibase_bar1_read,
    .write = pcibase_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps bar2_ops = {
    .read = pcibase_bar2_read,
    .write = pcibase_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps bar3_ops = {
    .read = pcibase_bar3_read,
    .write = pcibase_bar3_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps *bar_ops[4] = {
    &bar0_ops, &bar1_ops, &bar2_ops, &bar3_ops
};

/* Helper: update IRQ line based on irqstat & irqmask */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->irqstat & s->irqmask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Reset function */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear all shadow registers */
    s->control = 0;
    s->irqstat = 0;
    s->irqmask = 0;
    s->i2c_dev_addr = 0;
    s->i2c_ctrl = 0;
    s->i2c_data = 0;
    s->i2c_byte_addr = 0;
    s->mpu1_ctrl = 0;
    s->mpu2_ctrl = 0;
    s->ac97_cmd = ICE1712_AC97_READY; /* ready after reset */
    s->ac97_index = 0;
    s->ac97_data = 0;
    s->ireg_index = 0;
    s->ireg_data = 0;
    s->concap_addr = 0;
    s->concap_count = 0;
    memset(s->iregs, 0, sizeof(s->iregs));

    s->ddma_addr = 0;
    s->ddma_count = 0;
    s->ddma_mode = 0;
    s->ddma_unknown15 = 0;
    s->ddma_ds_index = 0;
    s->ddma_ds_data = 0;
    s->ddma_intstat = 0;
    s->ddma_intmask = 0;
    memset(s->ds_regs, 0, sizeof(s->ds_regs));

    s->mt_rate = 0;
    s->mt_playback_control = 0;
    s->mt_playback_addr = 0;
    s->mt_playback_size = 0;
    s->mt_playback_count = 0;
    s->mt_capture_addr = 0;
    s->mt_capture_size = 0;
    s->mt_capture_count = 0;
    s->mt_monitor_routectrl = 0;
    s->mt_route_psdout03 = 0;
    s->mt_route_capture = 0;
    s->mt_route_spdout = 0;
    s->mt_monitor_index = 0;
    s->mt_monitor_volume = 0;
    s->mt_monitor_rate = 0;
    s->mt_monitor_peakindex = 0;
    s->mt_monitor_peakdata = 0;
    s->mt_irq = 0;

    /* Load default EEPROM */
    memcpy(s->eeprom, default_eeprom, sizeof(s->eeprom));
    s->i2c_current_byte = 0;

    s->playback_addr = 0;
    s->capture_addr = 0;

    s->reset_active = false;
    s->power_state = 0;

    pcibase_update_irq(s);
}

/* BAR registration: modified to use per-BAR ops */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    const MemoryRegionOps *ops = bar_ops[bi->index];

    s->bar_opaques[bi->index].dev = s;
    s->bar_opaques[bi->index].bar = bi->index;

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), ops, &s->bar_opaques[bi->index], bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), ops, &s->bar_opaques[bi->index], bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* Realize function */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BARs: all PIO, size 0x100 */
    s->num_bars = 4;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_PIO, 0x100, "bar0-main"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_PIO, 0x100, "bar1-ddma"};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_PIO, 0x100, "bar2-dmapath"};
    s->bar_info[3] = (BARInfo){3, BAR_TYPE_PIO, 0x100, "bar3-profi"};

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X for this device (legacy interrupts only) */
    s->has_msi = false;
    s->has_msix = false;
}

/* Uninit */
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

/* Minimal VMState */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_ice1712_pci",
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
