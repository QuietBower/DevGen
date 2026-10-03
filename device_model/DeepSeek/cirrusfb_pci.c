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

#define TYPE_PCIBASE_DEVICE "cirrusfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Standard VGA I/O port addresses */
#define VGA_IO_BASE 0x3C0
#define VGA_IO_SIZE 0x20

/* PCI configuration */
#define PCI_CLASS_DISPLAY_VGA 0x0300

/* Board info enum from driver */
enum cirrus_board {
    BT_NONE = 0,
    BT_SD64,        /* GD5434 */
    BT_PICCOLO,     /* GD5426 */
    BT_PICASSO,     /* GD5426 or GD5428 */
    BT_SPECTRUM,    /* GD5426 or GD5428 */
    BT_PICASSO4,    /* GD5446 */
    BT_ALPINE,      /* GD543x/4x */
    BT_GD5480,
    BT_LAGUNA,      /* GD5462/64 */
    BT_LAGUNAB,     /* GD5465 */
};

/* Standard VGA register indices and ports */
#define VGA_SEQ_INDEX           0x3C4
#define VGA_SEQ_DATA            0x3C5
#define VGA_CRTC_INDEX          0x3D4
#define VGA_CRTC_DATA           0x3D5
#define VGA_GFX_INDEX           0x3CE
#define VGA_GFX_DATA            0x3CF
#define VGA_ATC_INDEX           0x3C0
#define VGA_ATC_DATA_R          0x3C1
#define VGA_ATC_DATA_W          0x3C0
#define VGA_MIS_W               0x3C2
#define VGA_MIS_R               0x3CC
#define VGA_PEL_MSK             0x3C6
#define VGA_PEL_IR              0x3C7
#define VGA_PEL_IW              0x3C8
#define VGA_PEL_D               0x3C9
#define VGA_IS1_R               0x3DA
#define VGA_FTC_R               0x3CA

/* Standard VGA register names for internal arrays */
#define VGA_SEQ_RESET           0x00
#define VGA_SEQ_CLOCK_MODE      0x01
#define VGA_SEQ_PLANE_WRITE     0x02
#define VGA_SEQ_CHARACTER_MAP   0x03
#define VGA_SEQ_MEMORY_MODE     0x04
#define VGA_CRTC_H_TOTAL        0x00
#define VGA_CRTC_H_DISP         0x01
#define VGA_CRTC_H_BLANK_START  0x02
#define VGA_CRTC_H_BLANK_END    0x03
#define VGA_CRTC_H_SYNC_START   0x04
#define VGA_CRTC_H_SYNC_END     0x05
#define VGA_CRTC_V_TOTAL        0x06
#define VGA_CRTC_OVERFLOW       0x07
#define VGA_CRTC_PRESET_ROW     0x08
#define VGA_CRTC_MAX_SCAN       0x09
#define VGA_CRTC_CURSOR_START   0x0A
#define VGA_CRTC_CURSOR_END     0x0B
#define VGA_CRTC_START_HI       0x0C
#define VGA_CRTC_START_LO       0x0D
#define VGA_CRTC_CURSOR_HI      0x0E
#define VGA_CRTC_CURSOR_LO      0x0F
#define VGA_CRTC_V_SYNC_START   0x10
#define VGA_CRTC_V_SYNC_END     0x11
#define VGA_CRTC_V_DISP_END     0x12
#define VGA_CRTC_OFFSET         0x13
#define VGA_CRTC_UNDERLINE      0x14
#define VGA_CRTC_V_BLANK_START  0x15
#define VGA_CRTC_V_BLANK_END    0x16
#define VGA_CRTC_MODE           0x17
#define VGA_CRTC_LINE_COMPARE   0x18
#define VGA_GFX_SR_VALUE        0x00
#define VGA_GFX_SR_ENABLE       0x01
#define VGA_GFX_COMPARE_VALUE   0x02
#define VGA_GFX_DATA_ROTATE     0x03
#define VGA_GFX_PLANE_READ      0x04
#define VGA_GFX_MODE            0x05
#define VGA_GFX_MISC            0x06
#define VGA_GFX_COMPARE_MASK    0x07
#define VGA_GFX_BIT_MASK        0x08
#define VGA_ATC_PALETTE0        0x00
#define VGA_ATC_PALETTE1        0x01
#define VGA_ATC_PALETTE2        0x02
#define VGA_ATC_PALETTE3        0x03
#define VGA_ATC_PALETTE4        0x04
#define VGA_ATC_PALETTE5        0x05
#define VGA_ATC_PALETTE6        0x06
#define VGA_ATC_PALETTE7        0x07
#define VGA_ATC_PALETTE8        0x08
#define VGA_ATC_PALETTE9        0x09
#define VGA_ATC_PALETTEA        0x0A
#define VGA_ATC_PALETTEB        0x0B
#define VGA_ATC_PALETTEC        0x0C
#define VGA_ATC_PALETTED        0x0D
#define VGA_ATC_PALETTEE        0x0E
#define VGA_ATC_PALETTEF        0x0F
#define VGA_ATC_MODE            0x10
#define VGA_ATC_OVERSCAN        0x11
#define VGA_ATC_PLANE_ENABLE    0x12
#define VGA_ATC_COLOR_PAGE      0x13

/* Cirrus Logic extended register indices */
/* Sequencer extensions */
#define CL_SEQR6                0x06
#define CL_SEQR7                0x07
#define CL_SEQR10               0x10
#define CL_SEQR11               0x11
#define CL_SEQR12               0x12
#define CL_SEQR13               0x13
#define CL_SEQR14               0x14
#define CL_SEQR16               0x16
#define CL_SEQR17               0x17
#define CL_SEQR18               0x18
#define CL_SEQR1E               0x1E
#define CL_SEQR1F               0x1F
#define CL_SEQRF                0x0F
#define CL_SEQRE                0x0E

/* CRTC extensions */
#define CL_CRT1A                0x1A
#define CL_CRT1B                0x1B
#define CL_CRT1D                0x1D
#define CL_CRT1E                0x1E
#define CL_CRT24                0x24
#define CL_CRT51                0x51

/* Graphics Controller extensions */
#define CL_GRB                  0x0B
#define CL_GRC                  0x0C
#define CL_GRD                  0x0D
#define CL_GRE                  0x0E
#define CL_GR10                 0x10
#define CL_GR11                 0x11
#define CL_GR12                 0x12
#define CL_GR13                 0x13
#define CL_GR14                 0x14
#define CL_GR15                 0x15
#define CL_GR20                 0x20
#define CL_GR21                 0x21
#define CL_GR22                 0x22
#define CL_GR23                 0x23
#define CL_GR24                 0x24
#define CL_GR25                 0x25
#define CL_GR26                 0x26
#define CL_GR27                 0x27
#define CL_GR28                 0x28
#define CL_GR29                 0x29
#define CL_GR2A                 0x2A
#define CL_GR2C                 0x2C
#define CL_GR2D                 0x2D
#define CL_GR2E                 0x2E
#define CL_GR2F                 0x2F
#define CL_GR30                 0x30
#define CL_GR31                 0x31
#define CL_GR32                 0x32
#define CL_GR33                 0x33

/* Attribute Controller extensions */
#define CL_AR33                 0x33

/* Miscellaneous registers (not part of VGA standard index pairs) */
#define CL_VSSM                 0x46E8
#define CL_VSSM2                0x46E9
#define CL_POS102               0x46EA

/* PCI device ID for Cirrus CL-GD5436 (matching driver's first entry) */
#define PCI_DEVICE_ID_CIRRUS_5436 0x00A0

#define FRAMEBUFFER_SIZE (2 * MiB)
#define MMIO_BAR_SIZE (0x1000)

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Framebuffer memory (BAR0) */
    MemoryRegion vram;

    /* MMIO register region (BAR1) */
    MemoryRegion mmio;

    /* VGA legacy I/O region (unmapped from BARs, directly placed at 0x3C0) */
    MemoryRegion vga_io;

    /* Sequencer state */
    uint8_t seq_index;
    uint8_t seq_regs[64];

    /* CRTC state */
    uint8_t crtc_index;
    uint8_t crtc_regs[256];

    /* Graphics Controller state */
    uint8_t gfx_index;
    uint8_t gfx_regs[256];

    /* Attribute Controller state */
    uint8_t attr_index;
    uint8_t attr_regs[64];
    bool attr_flip_flop;

    /* DAC state */
    uint8_t dac_mask;
    uint8_t dac_read_index;
    uint8_t dac_write_index;
    uint8_t dac_subread;   /* sub-index for read (0=R, 1=G, 2=B) */
    uint8_t dac_subwrite;  /* sub-index for write (0=R, 1=G, 2=B) */
    uint8_t dac_state; /* from DAC read index register */
    uint8_t dac_palette[768]; /* 256 entries * 3 channels (R,G,B) */

    /* Misc output */
    uint8_t misc_output;

    /* Input status 1 register (vertical retrace) */
    bool vretrace;

    /* Feature control register */
    uint8_t feature_ctrl;

    /* Board type from PCI device ID (set at realize) */
    enum cirrus_board btype;

    /* Saved SFR value (Zorro only, not used for PCI) */
    uint8_t sfr;
};

/* VGA I/O ops (legacy ports 0x3C0-0x3DF) */
static uint64_t vga_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0;

    addr += VGA_IO_BASE;

    switch (addr) {
    case VGA_SEQ_INDEX: /* 0x3C4 */
        val = s->seq_index;
        break;
    case VGA_SEQ_DATA: /* 0x3C5 */
        if (s->seq_index < 64) {
            val = s->seq_regs[s->seq_index];
        }
        break;
    case VGA_CRTC_INDEX: /* 0x3D4 */
        val = s->crtc_index;
        break;
    case VGA_CRTC_DATA: /* 0x3D5 */
        if ((unsigned)s->crtc_index < 256) {
            val = s->crtc_regs[s->crtc_index];
        }
        break;
    case VGA_GFX_INDEX: /* 0x3CE */
        val = s->gfx_index;
        break;
    case VGA_GFX_DATA: /* 0x3CF */
        if ((unsigned)s->gfx_index < 256) {
            val = s->gfx_regs[s->gfx_index];
        }
        break;
    case VGA_ATC_INDEX: /* 0x3C0 - Input Status Register 0 */
        /* Reads from 0x3C0 return Input Status Register 0; we return 0 */
        val = 0;
        break;
    case VGA_ATC_DATA_R: /* 0x3C1 */
        if (s->attr_index < 64) {
            val = s->attr_regs[s->attr_index];
        }
        break;
    case VGA_MIS_R: /* 0x3CC */
        val = s->misc_output;
        break;
    case VGA_PEL_MSK: /* 0x3C6 */
        val = s->dac_mask;
        break;
    case VGA_PEL_IR: /* 0x3C7 */
        val = s->dac_state;
        break;
    case VGA_PEL_IW: /* 0x3C8 */
        val = s->dac_write_index;
        break;
    case VGA_PEL_D: /* 0x3C9 */
        if ((unsigned)s->dac_read_index < 256) {
            val = s->dac_palette[s->dac_read_index * 3 + s->dac_subread];
            /* increment sub-index and possibly advance read_index */
            s->dac_subread++;
            if (s->dac_subread >= 3) {
                s->dac_subread = 0;
                s->dac_read_index = (s->dac_read_index + 1) & 0xFF;
            }
        }
        break;
    case VGA_IS1_R: /* 0x3DA */
        /* Returning 0x08 indicates display is active, vertical retrace inactive.
         * Reading this port also resets the attribute flip-flop. */
        val = 0x08;
        s->attr_flip_flop = false;
        break;
    case VGA_FTC_R: /* 0x3CA */
        val = s->feature_ctrl;
        break;
    default:
        break;
    }

    return val;
}

static void vga_ioport_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t v = val & 0xFF;

    addr += VGA_IO_BASE;

    switch (addr) {
    case VGA_SEQ_INDEX: /* 0x3C4 */
        s->seq_index = v;
        break;
    case VGA_SEQ_DATA: /* 0x3C5 */
        if (s->seq_index < 64) {
            s->seq_regs[s->seq_index] = v;
        }
        break;
    case VGA_CRTC_INDEX: /* 0x3D4 */
        s->crtc_index = v;
        break;
    case VGA_CRTC_DATA: /* 0x3D5 */
        if ((unsigned)s->crtc_index < 256) {
            s->crtc_regs[s->crtc_index] = v;
        }
        break;
    case VGA_GFX_INDEX: /* 0x3CE */
        s->gfx_index = v;
        break;
    case VGA_GFX_DATA: /* 0x3CF */
        if ((unsigned)s->gfx_index < 256) {
            s->gfx_regs[s->gfx_index] = v;
            /* Handle blitter command */
            if (s->gfx_index == CL_GR31 && (v & 0x02)) {
                /* BLT start */
                s->gfx_regs[CL_GR31] |= 0x08; /* set busy bit */

                /* Execute blit operation synchronously */
                uint32_t width = (s->gfx_regs[CL_GR21] << 8) | s->gfx_regs[CL_GR20];
                uint32_t height = (s->gfx_regs[CL_GR23] << 8) | s->gfx_regs[CL_GR22];
                uint32_t src_pitch = (s->gfx_regs[CL_GR27] << 8) | s->gfx_regs[CL_GR26];
                uint32_t dst_pitch = (s->gfx_regs[CL_GR25] << 8) | s->gfx_regs[CL_GR24];
                uint32_t src = (s->gfx_regs[CL_GR2E] << 16) | (s->gfx_regs[CL_GR2D] << 8) | s->gfx_regs[CL_GR2C];
                uint32_t dst = (s->gfx_regs[CL_GR2A] << 16) | (s->gfx_regs[CL_GR29] << 8) | s->gfx_regs[CL_GR28];
                uint8_t mode = s->gfx_regs[CL_GR30];
                uint8_t rop = s->gfx_regs[CL_GR32];

                uint8_t *vram = memory_region_get_ram_ptr(&s->vram);
                uint32_t framebuffer_size = memory_region_size(&s->vram);

                /* Simple SRCCOPY (rop 0x0D) */
                if (rop == 0x0D) {
                    bool reverse = (mode & 0x01) != 0;
                    if (!reverse) {
                        for (int y = 0; y <= height; y++) {
                            for (int x = 0; x <= width; x++) {
                                uint32_t src_offset = src + y * src_pitch + x;
                                uint32_t dst_offset = dst + y * dst_pitch + x;
                                if (src_offset < framebuffer_size && dst_offset < framebuffer_size) {
                                    vram[dst_offset] = vram[src_offset];
                                }
                            }
                        }
                    } else {
                        for (int y = height; y >= 0; y--) {
                            for (int x = width; x >= 0; x--) {
                                uint32_t src_offset = src + y * src_pitch + x;
                                uint32_t dst_offset = dst + y * dst_pitch + x;
                                if (src_offset < framebuffer_size && dst_offset < framebuffer_size) {
                                    vram[dst_offset] = vram[src_offset];
                                }
                            }
                        }
                    }
                }

                s->gfx_regs[CL_GR31] &= ~0x08; /* clear busy */
            }
        }
        break;
    case VGA_ATC_INDEX: /* 0x3C0 */
        if (!s->attr_flip_flop) {
            s->attr_index = v & 0x1F; /* index write */
            s->attr_flip_flop = true;
        } else {
            if (s->attr_index < 64) {
                s->attr_regs[s->attr_index] = v;
            }
            s->attr_flip_flop = false;
        }
        break;
    case VGA_MIS_W: /* 0x3C2 */
        s->misc_output = v;
        break;
    case VGA_PEL_MSK: /* 0x3C6 */
        s->dac_mask = v;
        break;
    case VGA_PEL_IR: /* 0x3C7 */
        s->dac_read_index = v;
        s->dac_subread = 0;
        s->dac_state = 0; /* indicate read mode */
        break;
    case VGA_PEL_IW: /* 0x3C8 */
        s->dac_write_index = v;
        s->dac_subwrite = 0;
        break;
    case VGA_PEL_D: /* 0x3C9 */
        if ((unsigned)s->dac_write_index < 256) {
            s->dac_palette[s->dac_write_index * 3 + s->dac_subwrite] = v;
            s->dac_subwrite++;
            if (s->dac_subwrite >= 3) {
                s->dac_subwrite = 0;
                s->dac_write_index = (s->dac_write_index + 1) & 0xFF;
            }
        }
        break;
    case VGA_IS1_R: /* 0x3DA */
        /* Writing to this port has no effect; but reading resets flip-flop (handled in read) */
        break;
    case VGA_FTC_R: /* 0x3CA */
        s->feature_ctrl = v;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps vga_io_ops = {
    .read = vga_ioport_read,
    .write = vga_ioport_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* MMIO register ops (memory-mapped BAR1) - same registers as legacy I/O but at direct offsets */
static uint64_t mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0;

    switch (addr) {
    case VGA_SEQ_INDEX: /* 0x3C4 */
        val = s->seq_index;
        break;
    case VGA_SEQ_DATA: /* 0x3C5 */
        if (s->seq_index < 64) {
            val = s->seq_regs[s->seq_index];
        }
        break;
    case VGA_CRTC_INDEX: /* 0x3D4 */
        val = s->crtc_index;
        break;
    case VGA_CRTC_DATA: /* 0x3D5 */
        if ((unsigned)s->crtc_index < 256) {
            val = s->crtc_regs[s->crtc_index];
        }
        break;
    case VGA_GFX_INDEX: /* 0x3CE */
        val = s->gfx_index;
        break;
    case VGA_GFX_DATA: /* 0x3CF */
        if ((unsigned)s->gfx_index < 256) {
            val = s->gfx_regs[s->gfx_index];
        }
        break;
    case VGA_ATC_INDEX: /* 0x3C0 - Input Status Register 0 */
        val = 0;
        break;
    case VGA_ATC_DATA_R: /* 0x3C1 */
        if (s->attr_index < 64) {
            val = s->attr_regs[s->attr_index];
        }
        break;
    case VGA_MIS_R: /* 0x3CC */
        val = s->misc_output;
        break;
    case VGA_PEL_MSK: /* 0x3C6 */
        val = s->dac_mask;
        break;
    case VGA_PEL_IR: /* 0x3C7 */
        val = s->dac_state;
        break;
    case VGA_PEL_IW: /* 0x3C8 */
        val = s->dac_write_index;
        break;
    case VGA_PEL_D: /* 0x3C9 */
        if ((unsigned)s->dac_read_index < 256) {
            val = s->dac_palette[s->dac_read_index * 3 + s->dac_subread];
            s->dac_subread++;
            if (s->dac_subread >= 3) {
                s->dac_subread = 0;
                s->dac_read_index = (s->dac_read_index + 1) & 0xFF;
            }
        }
        break;
    case VGA_IS1_R: /* 0x3DA */
        val = 0x08;
        s->attr_flip_flop = false;
        break;
    case VGA_FTC_R: /* 0x3CA */
        val = s->feature_ctrl;
        break;
    default:
        break;
    }

    return val;
}

static void mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t v = val & 0xFF;

    switch (addr) {
    case VGA_SEQ_INDEX: /* 0x3C4 */
        s->seq_index = v;
        break;
    case VGA_SEQ_DATA: /* 0x3C5 */
        if (s->seq_index < 64) {
            s->seq_regs[s->seq_index] = v;
        }
        break;
    case VGA_CRTC_INDEX: /* 0x3D4 */
        s->crtc_index = v;
        break;
    case VGA_CRTC_DATA: /* 0x3D5 */
        if ((unsigned)s->crtc_index < 256) {
            s->crtc_regs[s->crtc_index] = v;
        }
        break;
    case VGA_GFX_INDEX: /* 0x3CE */
        s->gfx_index = v;
        break;
    case VGA_GFX_DATA: /* 0x3CF */
        if ((unsigned)s->gfx_index < 256) {
            s->gfx_regs[s->gfx_index] = v;
            /* Handle blitter command */
            if (s->gfx_index == CL_GR31 && (v & 0x02)) {
                /* BLT start */
                s->gfx_regs[CL_GR31] |= 0x08; /* set busy bit */

                /* Execute blit operation synchronously */
                uint32_t width = (s->gfx_regs[CL_GR21] << 8) | s->gfx_regs[CL_GR20];
                uint32_t height = (s->gfx_regs[CL_GR23] << 8) | s->gfx_regs[CL_GR22];
                uint32_t src_pitch = (s->gfx_regs[CL_GR27] << 8) | s->gfx_regs[CL_GR26];
                uint32_t dst_pitch = (s->gfx_regs[CL_GR25] << 8) | s->gfx_regs[CL_GR24];
                uint32_t src = (s->gfx_regs[CL_GR2E] << 16) | (s->gfx_regs[CL_GR2D] << 8) | s->gfx_regs[CL_GR2C];
                uint32_t dst = (s->gfx_regs[CL_GR2A] << 16) | (s->gfx_regs[CL_GR29] << 8) | s->gfx_regs[CL_GR28];
                uint8_t mode = s->gfx_regs[CL_GR30];
                uint8_t rop = s->gfx_regs[CL_GR32];

                uint8_t *vram = memory_region_get_ram_ptr(&s->vram);
                uint32_t framebuffer_size = memory_region_size(&s->vram);

                /* Simple SRCCOPY (rop 0x0D) */
                if (rop == 0x0D) {
                    bool reverse = (mode & 0x01) != 0;
                    if (!reverse) {
                        for (int y = 0; y <= height; y++) {
                            for (int x = 0; x <= width; x++) {
                                uint32_t src_offset = src + y * src_pitch + x;
                                uint32_t dst_offset = dst + y * dst_pitch + x;
                                if (src_offset < framebuffer_size && dst_offset < framebuffer_size) {
                                    vram[dst_offset] = vram[src_offset];
                                }
                            }
                        }
                    } else {
                        for (int y = height; y >= 0; y--) {
                            for (int x = width; x >= 0; x--) {
                                uint32_t src_offset = src + y * src_pitch + x;
                                uint32_t dst_offset = dst + y * dst_pitch + x;
                                if (src_offset < framebuffer_size && dst_offset < framebuffer_size) {
                                    vram[dst_offset] = vram[src_offset];
                                }
                            }
                        }
                    }
                }

                s->gfx_regs[CL_GR31] &= ~0x08; /* clear busy */
            }
        }
        break;
    case VGA_ATC_INDEX: /* 0x3C0 */
        if (!s->attr_flip_flop) {
            s->attr_index = v & 0x1F; /* index write */
            s->attr_flip_flop = true;
        } else {
            if (s->attr_index < 64) {
                s->attr_regs[s->attr_index] = v;
            }
            s->attr_flip_flop = false;
        }
        break;
    case VGA_MIS_W: /* 0x3C2 */
        s->misc_output = v;
        break;
    case VGA_PEL_MSK: /* 0x3C6 */
        s->dac_mask = v;
        break;
    case VGA_PEL_IR: /* 0x3C7 */
        s->dac_read_index = v;
        s->dac_subread = 0;
        s->dac_state = 0;
        break;
    case VGA_PEL_IW: /* 0x3C8 */
        s->dac_write_index = v;
        s->dac_subwrite = 0;
        break;
    case VGA_PEL_D: /* 0x3C9 */
        if ((unsigned)s->dac_write_index < 256) {
            s->dac_palette[s->dac_write_index * 3 + s->dac_subwrite] = v;
            s->dac_subwrite++;
            if (s->dac_subwrite >= 3) {
                s->dac_subwrite = 0;
                s->dac_write_index = (s->dac_write_index + 1) & 0xFF;
            }
        }
        break;
    case VGA_IS1_R: /* 0x3DA */
        /* Writing to this port does nothing */
        break;
    case VGA_FTC_R: /* 0x3CA */
        s->feature_ctrl = v;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps mmio_ops = {
    .read = mmio_read,
    .write = mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset standard VGA registers */
    s->seq_index = 0;
    memset(s->seq_regs, 0, sizeof(s->seq_regs));
    s->crtc_index = 0;
    memset(s->crtc_regs, 0, sizeof(s->crtc_regs));
    s->gfx_index = 0;
    memset(s->gfx_regs, 0, sizeof(s->gfx_regs));
    s->attr_index = 0;
    memset(s->attr_regs, 0, sizeof(s->attr_regs));
    s->attr_flip_flop = false;
    s->dac_mask = 0xFF;
    s->dac_read_index = 0;
    s->dac_write_index = 0;
    s->dac_subread = 0;
    s->dac_subwrite = 0;
    s->dac_state = 0;
    memset(s->dac_palette, 0, sizeof(s->dac_palette));
    s->misc_output = 0;
    s->vretrace = false;
    s->feature_ctrl = 0;
    s->sfr = 0;

    /* Set default values expected by driver */
    s->seq_regs[VGA_SEQ_RESET] = 0x03;
    s->seq_regs[VGA_SEQ_CLOCK_MODE] = 0x21;
    s->seq_regs[VGA_SEQ_PLANE_WRITE] = 0xFF;
    s->seq_regs[VGA_SEQ_CHARACTER_MAP] = 0x00;
    s->seq_regs[VGA_SEQ_MEMORY_MODE] = 0x0A;
    s->seq_regs[CL_SEQR7] = 0xF0;
    s->seq_regs[CL_SEQRF] = 0x18; /* 2MB memory size, 64-bit bus width */
    s->seq_regs[CL_SEQR1F] = 0x1E;

    /* Typical CRTC values for text mode */
    s->crtc_regs[VGA_CRTC_H_TOTAL] = 0x71;
    s->crtc_regs[VGA_CRTC_H_DISP] = 0x50;
    s->crtc_regs[VGA_CRTC_H_BLANK_START] = 0x5A;
    s->crtc_regs[VGA_CRTC_H_BLANK_END] = 0x8F;
    s->crtc_regs[VGA_CRTC_H_SYNC_START] = 0x5E;
    s->crtc_regs[VGA_CRTC_H_SYNC_END] = 0x2B;
    s->crtc_regs[VGA_CRTC_V_TOTAL] = 0x1F;
    s->crtc_regs[VGA_CRTC_OVERFLOW] = 0x3F;
    s->crtc_regs[VGA_CRTC_PRESET_ROW] = 0x00;
    s->crtc_regs[VGA_CRTC_MAX_SCAN] = 0x40;
    s->crtc_regs[VGA_CRTC_CURSOR_START] = 0x20;
    s->crtc_regs[VGA_CRTC_CURSOR_END] = 0x00;
    s->crtc_regs[VGA_CRTC_START_HI] = 0x00;
    s->crtc_regs[VGA_CRTC_START_LO] = 0x00;
    s->crtc_regs[VGA_CRTC_CURSOR_HI] = 0x00;
    s->crtc_regs[VGA_CRTC_CURSOR_LO] = 0x00;
    s->crtc_regs[VGA_CRTC_V_SYNC_START] = 0xFC;
    s->crtc_regs[VGA_CRTC_V_SYNC_END] = 0x83;
    s->crtc_regs[VGA_CRTC_V_DISP_END] = 0x1F;
    s->crtc_regs[VGA_CRTC_OFFSET] = 0x28;
    s->crtc_regs[VGA_CRTC_UNDERLINE] = 0x0D;
    s->crtc_regs[VGA_CRTC_V_BLANK_START] = 0xFC;
    s->crtc_regs[VGA_CRTC_V_BLANK_END] = 0x1F;
    s->crtc_regs[VGA_CRTC_MODE] = 0xA3;
    s->crtc_regs[VGA_CRTC_LINE_COMPARE] = 0xFF;

    /* Graphics controller defaults */
    s->gfx_regs[VGA_GFX_SR_VALUE] = 0x00;
    s->gfx_regs[VGA_GFX_SR_ENABLE] = 0x00;
    s->gfx_regs[VGA_GFX_COMPARE_VALUE] = 0x00;
    s->gfx_regs[VGA_GFX_DATA_ROTATE] = 0x00;
    s->gfx_regs[VGA_GFX_PLANE_READ] = 0x00;
    s->gfx_regs[VGA_GFX_MODE] = 0x00;
    s->gfx_regs[VGA_GFX_MISC] = 0x01;
    s->gfx_regs[VGA_GFX_COMPARE_MASK] = 0x0F;
    s->gfx_regs[VGA_GFX_BIT_MASK] = 0xFF;

    /* Attribute defaults */
    for (int i = 0; i < 16; i++) {
        s->attr_regs[i] = i;
    }
    s->attr_regs[VGA_ATC_MODE] = 0x01;
    s->attr_regs[VGA_ATC_OVERSCAN] = 0x00;
    s->attr_regs[VGA_ATC_PLANE_ENABLE] = 0x0F;
    s->attr_regs[VGA_ATC_COLOR_PAGE] = 0x00;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration (matching driver's PCI table) */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CIRRUS);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_CIRRUS_5436);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0);  /* No interrupt used */

    /* Set PCIe capabilities */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: framebuffer RAM */
    memory_region_init_ram(&s->vram, OBJECT(s), "cirrusfb-vram", FRAMEBUFFER_SIZE, errp);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->vram);

    /* BAR1: MMIO register region (memory-mapped VGA and extended registers) */
    memory_region_init_io(&s->mmio, OBJECT(s), &mmio_ops, s, "cirrusfb-mmio", MMIO_BAR_SIZE);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    /* Register legacy VGA I/O ports (0x3C0-0x3DF) */
    memory_region_init_io(&s->vga_io, OBJECT(s), &vga_io_ops, s, "cirrusfb-vga-io", VGA_IO_SIZE);
    memory_region_add_subregion(get_system_io(), VGA_IO_BASE, &s->vga_io);

    /* Additional I/O ports used by driver: 0x3B4/0x3B5 (monochrome CRTC) not used,
     * 0x46E8 (CL_VSSM) not used on PCI. */

    /* Set board type from device ID (SD64) */
    s->btype = BT_SD64;

    /* Initialize default register values */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    memory_region_del_subregion(get_system_io(), &s->vga_io);
    /* No MSI/MSIX to clean up */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "cirrusfb_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(seq_regs, PCIBaseState, 64),
        VMSTATE_UINT8(seq_index, PCIBaseState),
        VMSTATE_UINT8_ARRAY(crtc_regs, PCIBaseState, 256),
        VMSTATE_UINT8(crtc_index, PCIBaseState),
        VMSTATE_UINT8_ARRAY(gfx_regs, PCIBaseState, 256),
        VMSTATE_UINT8(gfx_index, PCIBaseState),
        VMSTATE_UINT8_ARRAY(attr_regs, PCIBaseState, 64),
        VMSTATE_UINT8(attr_index, PCIBaseState),
        VMSTATE_BOOL(attr_flip_flop, PCIBaseState),
        VMSTATE_UINT8(dac_mask, PCIBaseState),
        VMSTATE_UINT8(dac_read_index, PCIBaseState),
        VMSTATE_UINT8(dac_write_index, PCIBaseState),
        VMSTATE_UINT8(dac_subread, PCIBaseState),
        VMSTATE_UINT8(dac_subwrite, PCIBaseState),
        VMSTATE_UINT8(dac_state, PCIBaseState),
        VMSTATE_UINT8_ARRAY(dac_palette, PCIBaseState, 768),
        VMSTATE_UINT8(misc_output, PCIBaseState),
        VMSTATE_BOOL(vretrace, PCIBaseState),
        VMSTATE_UINT8(feature_ctrl, PCIBaseState),
        VMSTATE_UINT8(sfr, PCIBaseState),
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
