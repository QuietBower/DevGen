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

/* Additional include files retrieved from driver context */
/* (none) */

#define TYPE_PCIBASE_DEVICE "CyberPro_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI IDs */
#define PCI_VENDOR_ID_INTERG        0x1004
#define PCI_DEVICE_ID_INTERG_2000   0x2000
#define CYBERPRO_DEVICE_ID          PCI_DEVICE_ID_INTERG_2000
#define CYBERPRO_VENDOR_ID          PCI_VENDOR_ID_INTERG
#define CYBERPRO_CLASS              0x0300

/* Chip IDs */
#define ID_CYBERPRO_2000            1
#define ID_CYBERPRO_2010            2
#define ID_CYBERPRO_5000            3
#define ID_IGA_1682                 0

/* MMIO sizes and offsets */
#define MMIO_SIZE                   0x000c0000
#define MMIO_OFFSET                 0x00800000
#define VRAM_SIZE                   0x00800000  /* up to 8MB before registers */

/* DDC I2C registers */
#define DDC_REG                     0xb0
#define DDC_SCL_OUT                 (1 << 0)
#define DDC_SDA_OUT                 (1 << 4)
#define DDC_SCL_IN                  (1 << 2)
#define DDC_SDA_IN                  (1 << 6)

/* 2D engine (CO) registers */
#define CO_REG_CMD_H                0xbf07e
#define CO_CMD_H_BLITTER            0x0800
#define CO_REG_DEST_PTR             0xbf178
#define CO_REG_CONTROL              0xbf011
#define CO_REG_PIXHEIGHT            0xbf062
#define CO_REG_CMD_L                0xbf07c
#define CO_REG_PIXWIDTH             0xbf060
#define CO_FG_MIX_SRC               0x03
#define CO_REG_FGMIX                0xbf048
#define CO_CMD_L_PATTERN_FGCOL      0x8000
#define CO_REG_X_PHASE              0xbf078
#define CO_REG_FGCOLOUR             0xbf058
#define CO_CMD_L_INC_LEFT           0x0004
#define CO_CMD_H_FGSRCMAP           0x2000
#define CO_CMD_L_INC_UP             0x0002
#define CO_REG_SRC1_PTR             0xbf170
#define CO_CTRL_BUSY                0x80
#define CO_REG_DEST_WIDTH           0xbf218
#define CO_REG_SRC_WIDTH            0xbf018
#define CO_REG_PIXFMT               0xbf01c
#define CO_PIXFMT_24BPP             0x02
#define CO_PIXFMT_32BPP             0x03
#define CO_PIXFMT_16BPP             0x01
#define CO_PIXFMT_8BPP              0x00

/* Extended registers */
#define EXT_CRT_VRTOFL              0x11
#define EXT_DCLK_MULT               0xb0
#define EXT_MCLK_MULT               0xb2
#define EXT_MCLK_DIV                0xb3
#define EXT_DCLK_DIV                0xb1
#define EXT_SEQ_MISC                0x77
#define EXT_CRT_VRTOFL_LINECOMP10   0x10
#define EXT_CRT_VRTOFL_INTERLACE    0x20
#define EXT_DCLK_DIV_VFSEL          0x20
#define EXT_SEQ_MISC_24_RGB888      0x04
#define EXT_SEQ_MISC_32             0x03
#define RAMDAC_DAC8BIT              0x02
#define EXT_SEQ_MISC_16_RGB565      0x02
#define EXT_SEQ_MISC_16_RGB555      0x06
#define EXT_SEQ_MISC_8              0x01
#define RAMDAC_RAMPWRDN             0x01
#define RAMDAC_VREFEN               0x04
#define EXT_SEQ_MISC_16_RGB444      0x0a
#define RAMDAC_BYPASS               0x10
#define MEM_CTL2_64BIT              0x04
#define EXT_SYNC_CTL_VS_0           0x04
#define RAMDAC_DACPWRDN             0x40
#define EXT_SYNC_CTL_HS_NORMAL      0x00
#define EXT_SYNC_CTL_HS_0           0x01
#define EXT_SYNC_CTL_VS_NORMAL      0x00
#define EXT_SYNC_CTL                0x16
#define EXT_FUNC_CTL                0x3c
#define EXT_FUNC_CTL_EXTREGENBL     0x80
#define EXT_MEM_CTL2                0x72
#define EXT_MEM_CTL1                0x71
#define MEM_CTL2_SIZE_1MB           0x00
#define MEM_CTL2_SIZE_4MB           0x02
#define MEM_CTL2_SIZE_2MB           0x01
#define MEM_CTL2_SIZE_MASK          0x03
#define EXT_BUS_CTL                 0x30
#define EXT_BUS_CTL_PCIBURST_WRITE  0x20
#define EXT_BIU_MISC_LIN_ENABLE     0x01
#define EXT_BIU_MISC                0x33
#define EXT_BUS_CTL_PCIBURST_READ   0x80
#define EXT_ATTRIB_CTL_EXT          0x01
#define EXT_MEM_CTL0_MULTCAS        0x08
#define CURS_CTL                    0x56
#define CURS_H_PRESET               0x52
#define EXT_ATTRIB_CTL              0x57
#define EXT_SEG_READ_PTR            0x32
#define EXT_HIDDEN_CTL1             0x73
#define CURS_V_START                0x53
#define EXT_SEG_WRITE_PTR           0x31
#define EXT_CRT_TEST                0x13
#define EXT_OVERSCAN_RED            0x58
#define EXT_HIDDEN_CTL4             0x7a
#define EXT_BIU_MISC_COP_BFC        0x08
#define EXT_FIFO_CTL                0x74
#define EXT_OVERSCAN_GREEN          0x59
#define EXT_CRT_IRQ                 0x12
#define EXT_MEM_CTL0_7CLK           0x01
#define EXT_MEM_CTL0_RAS_1          0x02
#define EXT_OVERSCAN_BLUE           0x5a
#define CURS_V_PRESET               0x55
#define EXT_BIU_MISC_COP_ENABLE     0x04
#define CURS_H_START                0x50
#define EXT_MEM_CTL0                0x70

/* Encode bit helper macro from the driver */
#define ENCODE_BIT(v, b1, m, b2) ((((v) >> (b1)) & (m)) << (b2))

/* Number of palette entries */
#define NR_PALETTE                  256

/* Some stub macros to match driver (not needed in QEMU but for reference) */
#define cyber2000fb_i2c_register(cfb)   (0)
#define cyber2000fb_i2c_unregister(cfb) do { } while (0)

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
    MemoryRegion bar0_container;  /* container for BAR 0 (framebuffer + registers) */
    MemoryRegion vram_mr;         /* framebuffer RAM region (0..VRAM_SIZE-1) */
    MemoryRegion regs_mr;         /* MMIO register region (MMIO_OFFSET..end) */
    MemoryRegion bar_regions[6];  /* unused for BAR0 */
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint32_t chip_id;        /* Chip identification (e.g., ID_CYBERPRO_2000) */
    uint8_t  ext_func_ctl;   /* EXT_FUNC_CTL */
    uint8_t  ext_seq_misc;   /* EXT_SEQ_MISC */
    uint8_t  mem_ctl1;       /* EXT_MEM_CTL1 */
    uint8_t  mem_ctl2;       /* EXT_MEM_CTL2 */
    uint8_t  mclk_mult;      /* EXT_MCLK_MULT */
    uint8_t  mclk_div;       /* EXT_MCLK_DIV */
    uint8_t  dclk_mult;      /* EXT_DCLK_MULT */
    uint8_t  dclk_div;       /* EXT_DCLK_DIV */
    uint8_t  ramdac_ctrl;    /* RAMDAC control */
    uint8_t  ramdac_powerdown;
    uint8_t  crtc[19];       /* CRTC registers */
    uint8_t  ext_crt_vrtofl; /* EXT_CRT_VRTOFL */
    uint8_t  ext_sync_ctl;   /* EXT_SYNC_CTL */
    uint8_t  ext_bus_ctl;    /* EXT_BUS_CTL */
    uint8_t  ext_biu_misc;   /* EXT_BIU_MISC */
    uint8_t  ext_mem_ctl0;   /* EXT_MEM_CTL0 */
    uint8_t  ext_hidden_ctl1;/* EXT_HIDDEN_CTL1 */
    uint8_t  ext_fifo_ctl[2];/* EXT_FIFO_CTL */
    uint8_t  ext_overcan_r;  /* EXT_OVERSCAN_RED */
    uint8_t  ext_overcan_g;  /* EXT_OVERSCAN_GREEN */
    uint8_t  ext_overcan_b;  /* EXT_OVERSCAN_BLUE */
    uint8_t  ext_attrib_ctl; /* EXT_ATTRIB_CTL */
    uint8_t  curs_ctl;       /* CURS_CTL */
    uint8_t  curs_h_start[2];/* CURS_H_START */
    uint8_t  curs_v_start[2];/* CURS_V_START */
    uint8_t  curs_h_preset;  /* CURS_H_PRESET */
    uint8_t  curs_v_preset;  /* CURS_V_PRESET */
    uint8_t  ext_seg_write_ptr;/* EXT_SEG_WRITE_PTR */
    uint8_t  ext_seg_read_ptr; /* EXT_SEG_READ_PTR */
    uint8_t  ext_crt_test;   /* EXT_CRT_TEST */
    uint8_t  ext_crt_irq;    /* EXT_CRT_IRQ */
    uint8_t  ext_hidden_ctl4;/* EXT_HIDDEN_CTL4 */
    /* Palette */
    struct { uint8_t red, green, blue; } palette[NR_PALETTE];

    /* VGA indexed register emulation state */
    uint8_t gr_index;             /* graphics controller index (0x3ce) */
    uint8_t crtc_index;           /* CRTC index (0x3d4) */
    uint8_t seq_index;            /* sequencer index (0x3c4) */
    uint8_t attr_index;           /* attribute index (0x3c0) */
    uint8_t attr_flip_flop;       /* attribute address flip-flop (0=index, 1=data) */
    uint8_t dac_state;            /* DAC state: 0=idle, 1=reading, 2=writing */
    uint8_t dac_index;            /* current palette index */
    uint8_t dac_color;            /* DAC color component: 0=red, 1=green, 2=blue */
    uint8_t dac_mask;             /* DAC mask register (0x3c6) */
    uint8_t misc_output;          /* miscellaneous output register (0x3c2) */

    /* 2D engine (CO) registers */
    uint8_t  co_control;          /* CO_REG_CONTROL (0xbf011) */
    uint32_t co_cmd_h;            /* CO_REG_CMD_H (0xbf07e) */
    uint32_t co_cmd_l;            /* CO_REG_CMD_L (0xbf07c) */
    uint32_t co_dest_ptr;         /* CO_REG_DEST_PTR (0xbf178) */
    uint16_t co_pixwidth;         /* CO_REG_PIXWIDTH (0xbf060) */
    uint16_t co_pixheight;        /* CO_REG_PIXHEIGHT (0xbf062) */
    uint8_t  co_fgmix;            /* CO_REG_FGMIX (0xbf048) */
    uint8_t  co_x_phase;          /* CO_REG_X_PHASE (0xbf078) */
    uint32_t co_fgcolour;         /* CO_REG_FGCOLOUR (0xbf058) */
    uint32_t co_src1_ptr;         /* CO_REG_SRC1_PTR (0xbf170) */
    uint16_t co_dest_width;       /* CO_REG_DEST_WIDTH (0xbf218) */
    uint16_t co_src_width;        /* CO_REG_SRC_WIDTH (0xbf018) */
    uint8_t  co_pixfmt;           /* CO_REG_PIXFMT (0xbf01c) */

    /* Operational status flags */
    uint32_t status;               /* e.g., busy flags */

    /* Power management state (D0-D3) */
    uint8_t pm_state;
};

/* Forward declarations for MMIO register access */
static uint64_t pcibase_regs_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_regs_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static const MemoryRegionOps pcibase_regs_ops = {
    .read = pcibase_regs_read,
    .write = pcibase_regs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* IRQ not used by driver; placeholder removed */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No DMA in driver; placeholder removed */
}

/******************************************************************************
 * VGA-style indexed register helpers for the MMIO register region
 ******************************************************************************/

/* Map graphics controller index to the corresponding shadow field */
static uint8_t *pcibase_gr_field(PCIBaseState *s, uint8_t index)
{
    switch (index) {
    case EXT_CRT_VRTOFL:    return &s->ext_crt_vrtofl;
    case EXT_DCLK_MULT:     return &s->dclk_mult;
    case EXT_DCLK_DIV:      return &s->dclk_div;
    case EXT_MCLK_MULT:     return &s->mclk_mult;
    case EXT_MCLK_DIV:      return &s->mclk_div;
    case EXT_SEQ_MISC:      return &s->ext_seq_misc;
    case EXT_SYNC_CTL:      return &s->ext_sync_ctl;
    case EXT_FUNC_CTL:      return &s->ext_func_ctl;
    case EXT_MEM_CTL1:      return &s->mem_ctl1;
    case EXT_MEM_CTL2:      return &s->mem_ctl2;
    case EXT_BUS_CTL:       return &s->ext_bus_ctl;
    case EXT_BIU_MISC:      return &s->ext_biu_misc;
    case EXT_MEM_CTL0:      return &s->ext_mem_ctl0;
    case EXT_ATTRIB_CTL:    return &s->ext_attrib_ctl;
    case CURS_CTL:          return &s->curs_ctl;
    case CURS_H_START:      return &s->curs_h_start[0];
    case CURS_H_START+1:    return &s->curs_h_start[1];
    case CURS_V_START:      return &s->curs_v_start[0];
    case CURS_V_START+1:    return &s->curs_v_start[1];
    case CURS_H_PRESET:     return &s->curs_h_preset;
    case CURS_V_PRESET:     return &s->curs_v_preset;
    case EXT_SEG_WRITE_PTR: return &s->ext_seg_write_ptr;
    case EXT_SEG_READ_PTR:  return &s->ext_seg_read_ptr;
    case EXT_CRT_TEST:      return &s->ext_crt_test;
    case EXT_CRT_IRQ:       return &s->ext_crt_irq;
    case EXT_HIDDEN_CTL1:   return &s->ext_hidden_ctl1;
    case EXT_HIDDEN_CTL4:   return &s->ext_hidden_ctl4;
    case EXT_OVERSCAN_RED:  return &s->ext_overcan_r;
    case EXT_OVERSCAN_GREEN:return &s->ext_overcan_g;
    case EXT_OVERSCAN_BLUE: return &s->ext_overcan_b;
    case 0x74:              return &s->ext_fifo_ctl[0]; /* EXT_FIFO_CTL base */
    case 0x75:              return &s->ext_fifo_ctl[1];
    /* Additional registers used by driver (0x90, 0xb9, 0x14, 0x15, 0x10) */
    case 0x90: { static uint8_t dummy; return &dummy; }  /* PLL control */
    case 0xb9: { static uint8_t dummy; return &dummy; }  /* PLL control */
    case 0x14: { static uint8_t dummy; return &dummy; }  /* fetch */
    case 0x15: { static uint8_t dummy; return &dummy; }  /* fetch/pitch */
    case 0x10: { static uint8_t dummy; return &dummy; }  /* start addr high */
    default:
        /* Unknown register; return a dummy location to avoid crashes */
        printf("CyberPro: unknown GR index %02x access\n", index);
        { static uint8_t dummy; return &dummy; }
    }
}

/* Map CRTC index to shadow field */
static uint8_t *pcibase_crtc_field(PCIBaseState *s, uint8_t index)
{
    if (index >= 19) {
        printf("CyberPro: invalid CRTC index %02x\n", index);
        static uint8_t dummy;
        return &dummy;
    }
    return &s->crtc[index];
}

/* Sequencer registers are not individually shadowed; we'll use a static array for simplicity */
static uint8_t seq_regs[256];

/* Attribute registers (only 0x00-0x14 used) */
static uint8_t attr_regs[0x15];

static void pcibase_regs_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle based on offset within register region (relative to MMIO_OFFSET) */
    switch (addr) {
    /* Graphics controller index/data ports (0x3ce/0x3cf) */
    case 0x3ce:
        if (size == 1) s->gr_index = val;
        else printf("CyberPro: invalid write size %u to 0x3ce\n", size);
        break;
    case 0x3cf:
        if (size == 1) {
            uint8_t *field = pcibase_gr_field(s, s->gr_index);
            *field = val;
        } else {
            printf("CyberPro: invalid write size %u to 0x3cf\n", size);
        }
        break;

    /* CRTC index/data ports (0x3d4/0x3d5) */
    case 0x3d4:
        if (size == 1) s->crtc_index = val;
        break;
    case 0x3d5:
        if (size == 1) {
            uint8_t *field = pcibase_crtc_field(s, s->crtc_index);
            *field = val;
        }
        break;

    /* Sequencer index/data ports (0x3c4/0x3c5) */
    case 0x3c4:
        if (size == 1) s->seq_index = val;
        break;
    case 0x3c5:
        if (size == 1) {
            if (s->seq_index <= 4) /* sequencer has 5 registers (0-4) */
                seq_regs[s->seq_index] = val;
            else
                printf("CyberPro: unknown SEQ index %02x write\n", s->seq_index);
        }
        break;

    /* Attribute controller (0x3c0/0x3c1) */
    case 0x3c0:
        if (size != 1) break;
        if (s->attr_flip_flop == 0) {
            /* write index */
            s->attr_index = val & 0x1f;  /* 5-bit index */
            s->attr_flip_flop = 1;       /* next write is data */
        } else {
            /* write data */
            if (s->attr_index <= 0x14)
                attr_regs[s->attr_index] = val;
            s->attr_flip_flop = 0;       /* back to index */
        }
        break;
    case 0x3c1:
        /* read-only, handled in read */
        break;

    /* DAC registers */
    case 0x3c6:
        if (size == 1) s->dac_mask = val;
        break;
    case 0x3c7:  /* DAC read index (not used) */
        if (size == 1) { s->dac_state = 1; s->dac_index = val; }
        break;
    case 0x3c8:  /* DAC write index */
        if (size == 1) { s->dac_state = 2; s->dac_index = val; s->dac_color = 0; }
        break;
    case 0x3c9:  /* DAC data */
        if (size == 1) {
            if (s->dac_state == 2) {
                switch (s->dac_color) {
                case 0: s->palette[s->dac_index].red = val; break;
                case 1: s->palette[s->dac_index].green = val; break;
                case 2: s->palette[s->dac_index].blue = val; break;
                }
                s->dac_color = (s->dac_color + 1) % 3;
            } else if (s->dac_state == 1) {
                /* read mode: not used by driver */
            }
        }
        break;

    /* Miscellaneous output register (0x3c2) */
    case 0x3c2:
        if (size == 1) s->misc_output = val;
        break;

    /* Input status 0 register (0x3da) is read-only */

    /* 2D engine registers (CO) */
    case 0xbf011: /* CO_REG_CONTROL */
        if (size == 1) s->co_control = val;
        break;
    case 0xbf07e: /* CO_REG_CMD_H */
        if (size == 2) s->co_cmd_h = val;
        else if (size == 1) *(
            (uint8_t*)&s->co_cmd_h + (addr & 1)) = val;
        break;
    case 0xbf07c: /* CO_REG_CMD_L */
        if (size == 2) s->co_cmd_l = val;
        else if (size == 1) *(
            (uint8_t*)&s->co_cmd_l + (addr & 1)) = val;
        break;
    case 0xbf178: /* CO_REG_DEST_PTR */
        if (size == 4) s->co_dest_ptr = val;
        else if (size == 1) *(
            (uint8_t*)&s->co_dest_ptr + (addr & 3)) = val;
        break;
    case 0xbf060: /* CO_REG_PIXWIDTH */
        if (size == 2) s->co_pixwidth = val;
        else if (size == 1) *(
            (uint8_t*)&s->co_pixwidth + (addr & 1)) = val;
        break;
    case 0xbf062: /* CO_REG_PIXHEIGHT */
        if (size == 2) s->co_pixheight = val;
        else if (size == 1) *(
            (uint8_t*)&s->co_pixheight + (addr & 1)) = val;
        break;
    case 0xbf048: /* CO_REG_FGMIX */
        if (size == 1) s->co_fgmix = val;
        break;
    case 0xbf078: /* CO_REG_X_PHASE */
        if (size == 1) s->co_x_phase = val;
        break;
    case 0xbf058: /* CO_REG_FGCOLOUR */
        if (size == 4) s->co_fgcolour = val;
        else if (size == 1) *(
            (uint8_t*)&s->co_fgcolour + (addr & 3)) = val;
        break;
    case 0xbf170: /* CO_REG_SRC1_PTR */
        if (size == 4) s->co_src1_ptr = val;
        else if (size == 1) *(
            (uint8_t*)&s->co_src1_ptr + (addr & 3)) = val;
        break;
    case 0xbf218: /* CO_REG_DEST_WIDTH */
        if (size == 2) s->co_dest_width = val;
        else if (size == 1) *(
            (uint8_t*)&s->co_dest_width + (addr & 1)) = val;
        break;
    case 0xbf018: /* CO_REG_SRC_WIDTH */
        if (size == 2) s->co_src_width = val;
        else if (size == 1) *(
            (uint8_t*)&s->co_src_width + (addr & 1)) = val;
        break;
    case 0xbf01c: /* CO_REG_PIXFMT */
        if (size == 1) s->co_pixfmt = val;
        break;

    default:
        /* Unknown register; ignore */
        break;
    }
}

static uint64_t pcibase_regs_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t ret = 0;

    switch (addr) {
    /* Graphics controller data port (0x3cf) */
    case 0x3cf:
        if (size == 1) {
            ret = *pcibase_gr_field(s, s->gr_index);
        }
        break;
    /* CRTC data port (0x3d5) */
    case 0x3d5:
        if (size == 1) {
            ret = *pcibase_crtc_field(s, s->crtc_index);
        }
        break;
    /* Sequencer data port (0x3c5) */
    case 0x3c5:
        if (size == 1 && s->seq_index <= 4) {
            ret = seq_regs[s->seq_index];
        }
        break;
    /* Attribute data read port (0x3c1) */
    case 0x3c1:
        if (size == 1) {
            if (s->attr_index <= 0x14)
                ret = attr_regs[s->attr_index];
            /* reading should toggle flip-flop? Not in this driver */
        }
        break;
    /* DAC read (0x3c9) - not used by driver */
    /* Input status 0 (0x3da) - return 0 (not retrace) */
    case 0x3da:
        ret = 0x00;  /* bit 3 = vertical retrace; we return 0 */
        break;
    /* 2D engine registers */
    case 0xbf011:  /* CO_REG_CONTROL */
        if (size == 1) ret = s->co_control;
        break;
    case 0xbf07e: ret = (size == 2) ? s->co_cmd_h : (size == 1 ? ((uint8_t*)&s->co_cmd_h)[addr&1] : 0); break;
    case 0xbf07c: ret = (size == 2) ? s->co_cmd_l : (size == 1 ? ((uint8_t*)&s->co_cmd_l)[addr&1] : 0); break;
    case 0xbf178: ret = (size == 4) ? s->co_dest_ptr : (size == 1 ? ((uint8_t*)&s->co_dest_ptr)[addr&3] : 0); break;
    case 0xbf060: ret = (size == 2) ? s->co_pixwidth : (size == 1 ? ((uint8_t*)&s->co_pixwidth)[addr&1] : 0); break;
    case 0xbf062: ret = (size == 2) ? s->co_pixheight : (size == 1 ? ((uint8_t*)&s->co_pixheight)[addr&1] : 0); break;
    case 0xbf048: if (size == 1) ret = s->co_fgmix; break;
    case 0xbf078: if (size == 1) ret = s->co_x_phase; break;
    case 0xbf058: ret = (size == 4) ? s->co_fgcolour : (size == 1 ? ((uint8_t*)&s->co_fgcolour)[addr&3] : 0); break;
    case 0xbf170: ret = (size == 4) ? s->co_src1_ptr : (size == 1 ? ((uint8_t*)&s->co_src1_ptr)[addr&3] : 0); break;
    case 0xbf218: ret = (size == 2) ? s->co_dest_width : (size == 1 ? ((uint8_t*)&s->co_dest_width)[addr&1] : 0); break;
    case 0xbf018: ret = (size == 2) ? s->co_src_width : (size == 1 ? ((uint8_t*)&s->co_src_width)[addr&1] : 0); break;
    case 0xbf01c: if (size == 1) ret = s->co_pixfmt; break;
    default:
        /* Other reads: return 0 */
        break;
    }
    return ret;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    /* BAR0: if addr < VRAM_SIZE, it's framebuffer; else, register region */
    if (addr < VRAM_SIZE) {
        /* Read from framebuffer RAM; use memory_region_get_ram_ptr? No,
         * we can't easily access RAM from a custom read handler. But we've
         * configured the container so that reads < VRAM_SIZE go directly
         * to the RAM region, not this handler. So this function is never
         * called for addr < VRAM_SIZE. */
    } else {
        /* Delegate to registers: the regs_mr is separate, so this won't be called either. */
    }
    /* Should never be reached; include for completeness */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used; container directs writes to RAM or registers */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Legacy PIO not needed for probe */
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

    /* Reset VGA state to default values (matching typical VGA behavior) */
    s->gr_index = 0;
    s->crtc_index = 0;
    s->seq_index = 0;
    s->attr_index = 0;
    s->attr_flip_flop = 0;
    s->dac_state = 0;
    s->dac_index = 0;
    s->dac_color = 0;
    s->dac_mask = 0xff;
    s->misc_output = 0;

    /* Reset extended registers to default values as per BIOS initialization */
    s->ext_func_ctl = 0;
    s->ext_seq_misc = 0;
    s->mem_ctl1 = 0;
    s->mem_ctl2 = MEM_CTL2_SIZE_4MB;  /* Report 4MB VRAM */
    s->mclk_mult = 0x22;      /* arbitrary non-zero for probe */
    s->mclk_div = 0x55;       /* arbitrary non-zero */
    s->dclk_mult = 0;
    s->dclk_div = 0;
    s->ramdac_ctrl = 0;
    s->ramdac_powerdown = 0;
    memset(s->crtc, 0, sizeof(s->crtc));
    s->ext_crt_vrtofl = 0;
    s->ext_sync_ctl = 0;
    s->ext_bus_ctl = 0;
    s->ext_biu_misc = 0;
    s->ext_mem_ctl0 = 0;
    s->ext_hidden_ctl1 = 0;
    s->ext_fifo_ctl[0] = 0;
    s->ext_fifo_ctl[1] = 0;
    s->ext_overcan_r = 0;
    s->ext_overcan_g = 0;
    s->ext_overcan_b = 0;
    s->ext_attrib_ctl = 0;
    s->curs_ctl = 0;
    s->curs_h_start[0] = 0;
    s->curs_h_start[1] = 0;
    s->curs_v_start[0] = 0;
    s->curs_v_start[1] = 0;
    s->curs_h_preset = 0;
    s->curs_v_preset = 0;
    s->ext_seg_write_ptr = 0;
    s->ext_seg_read_ptr = 0;
    s->ext_crt_test = 0;
    s->ext_crt_irq = 0;
    s->ext_hidden_ctl4 = 0;
    memset(s->palette, 0, sizeof(s->palette));

    /* Reset 2D engine */
    s->co_control = 0;
    s->co_cmd_h = 0;
    s->co_cmd_l = 0;
    s->co_dest_ptr = 0;
    s->co_pixwidth = 0;
    s->co_pixheight = 0;
    s->co_fgmix = 0;
    s->co_x_phase = 0;
    s->co_fgcolour = 0;
    s->co_src1_ptr = 0;
    s->co_dest_width = 0;
    s->co_src_width = 0;
    s->co_pixfmt = 0;

    /* Reset sequencer and attribute registers */
    memset(seq_regs, 0, sizeof(seq_regs));
    memset(attr_regs, 0, sizeof(attr_regs));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, CYBERPRO_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, CYBERPRO_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CYBERPRO_CLASS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR 0: combined framebuffer + registers */
    /* Container region for BAR 0 */
    memory_region_init(&s->bar0_container, OBJECT(s), "cyberpro-bar0-container", 0x1000000);
    /* Framebuffer RAM (size VRAM_SIZE = 8MB) but BAR total is 16MB, so framebuffer up to 8MB, registers at 8MB */
    memory_region_init_ram(&s->vram_mr, OBJECT(s), "cyberpro-vram", VRAM_SIZE, errp);
    memory_region_add_subregion(&s->bar0_container, 0, &s->vram_mr);

    /* Register region (from offset MMIO_OFFSET to end) */
    memory_region_init_io(&s->regs_mr, OBJECT(s), &pcibase_regs_ops, s, "cyberpro-regs", 0x1000000 - MMIO_OFFSET);
    memory_region_add_subregion(&s->bar0_container, MMIO_OFFSET, &s->regs_mr);

    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0_container);

    /* MSI/MSI-X init removed; driver does not use MSI */
    /* DMA config removed; no explicit DMA */
    /* Timer config removed; no timers */

    /* Final state initialization */
    s->chip_id = ID_CYBERPRO_2000;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->pm_state = 0; /* D0 */
    /* Call reset to set all defaults */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* Uninit cleanup removed; no dynamic resources */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "CyberPro_pci",
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
