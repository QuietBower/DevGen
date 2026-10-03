/*
 * QEMU xHCI PCI device model for QEMU 8.2.10
 * Generated from Linux driver: xhci-pci.c
 * Implements minimal register interface to allow driver probing.
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

#define TYPE_PCIBASE_DEVICE "xhci_hcd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device ID from first entry in pci_device_id table (class match) */
#define VENDOR_ID 0xffff
#define DEVICE_ID 0xffff
#define CLASS_ID 0x0c0330

/* Register offset and bit definitions from xhci-pci.c and xhci.h */
#define PCI_DEVICE_ID_INTEL_CHERRYVIEW_XHCI		0x22b5
#define SSIC_PORT_NUM		2
#define SSIC_PORT_CFG2		0x880c
#define SSIC_PORT_CFG2_OFFSET	0x30
#define PROG_DONE		(1 << 30)
#define SSIC_PORT_UNUSED	(1 << 31)
#define SPARSE_DISABLE_BIT	17
#define SPARSE_CNTL_ENABLE	0xC12C

/* Hardware register macros from the driver */
#define XHCI_SBRN_OFFSET	(0x60)
#define HCC_MAX_PSA(p)		(1 << ((((p) >> 12) & 0xf) + 1))
#define PORT_PE		(1 << 1)
#define PORT_PLS_MASK	(0xf << 5)
#define XDEV_U3		(0x3 << 5)
#define CMD_RING_STATE_STOPPED         BIT(2)
#define RTSOFF_MASK	(~0x1f)
#define HC_LENGTH(p)		((p) & 0xff)
#define HCS_MAX_PORTS(p)	(((p) >> 24) & 0xff)
#define HCS_MAX_INTRS(p)	(((p) >> 8) & 0x7ff)
#define HC_VERSION(p)		(((p) >> 16) & 0xffff)
#define HCC_64BIT_ADDR		BIT(0)
#define HCS_MAX_SLOTS(p)	(((p) >> 0) & 0xff)
#define XHCI_MAX_PORTS(p)	(((p) & 0xff) << 24)
#define SLOT_FLAG	BIT(0)
#define XHCI_EXT_CAPS_ID(p)	(((p)>>0)&0xff)
#define XHCI_EXT_CAPS_VENDOR_INTEL	192
#define STS_SAVE	BIT(8)
#define STS_SRE		BIT(10)
#define CMD_RUN		XHCI_CMD_RUN
#define STS_HALT	XHCI_STS_HALT
#define CMD_CSS		BIT(8)
#define CMD_CRS		BIT(9)
#define STS_RESTORE	BIT(9)
#define STS_CNR		XHCI_STS_CNR
#define CMD_EIE		XHCI_CMD_EIE
#define XHCI_EXT_CAPS_PROTOCOL	2
#define XHCI_EXT_CAPS_DEBUG	10
#define XHCI_EXT_CAPS_LEGACY	1
#define XHCI_CMD_DEFAULT_TIMEOUT	5000
#define XHCI_PAGE_SIZE_MASK     0xffff
#define STS_FATAL	BIT(2)
#define CMD_HSEIE	XHCI_CMD_HSEIE
#define XHCI_HCC_PARAMS_OFFSET	0x10
#define XHCI_EXT_CAPS_NEXT(p)	(((p)>>8)&0xff)
#define XHCI_HCC_EXT_CAPS(p)	(((p)>>16)&0xffff)
#define XHCI_CMD_RUN		(1 << 0)
#define XHCI_STS_HALT		(1<<0)
#define PORT_CONNECT	(1 << 0)
#define PORT_CSC	(1 << 17)
#define PORT_CAS	(1 << 24)
#define STS_EINT	BIT(3)
#define XDEV_RESUME	(0xf << 5)
#define PORT_CHANGE_MASK	(PORT_CSC | PORT_PEC | PORT_WRC | PORT_OCC | \
				 PORT_RC | PORT_PLC | PORT_CEC)
#define XHCI_STS_CNR		(1 << 11)
#define CMD_RING_CYCLE		BIT(0)
#define CMD_RING_PTR_MASK	GENMASK_ULL(63, 6)
#define XHCI_CMD_EIE		(1 << 2)
#define IMAN_IP			BIT(0)
#define IMAN_IE			BIT(1)
#define XHCI_CMD_HSEIE		(1 << 3)
#define ERST_DEFAULT_SEGS	2
#define HCS_ERST_MAX(p)		(((p) >> 4) & 0xf)
#define HCS_MAX_SCRATCHPAD(p)	(HCS_MAX_SP_HI(p) << 5 | HCS_MAX_SP_LO(p))
#define ERST_SIZE_MASK		(0xffff)
/* Fixed: QEMU-compatible masks to replace GENMASK_ULL */
#define ERST_BASE_ADDRESS_MASK	0xFFFFFFFFFFFFFFC0ULL /* bits 63:6 */
#define ERST_PTR_MASK		0xFFFFFFFFFFFFFFF0ULL /* bits 63:4 */
#define DBOFF_MASK	(0xfffffffc)
#define HCS_SLOTS_MASK		0xff
#define HCC_64BYTE_CONTEXT	BIT(2)
#define PORT_WKDISC_E	(1 << 26)
#define PORT_WKOC_E	(1 << 27)
#define PORT_WKCONN_E	(1 << 25)
#define PORT_WRC	(1 << 19)
#define PORT_PLC	(1 << 22)
#define PORT_CEC	(1 << 23)
#define PORT_OCC	(1 << 20)
#define PORT_RC		(1 << 21)
#define PORT_PEC	(1 << 18)
#define HCS_MAX_SP_HI(p)	(((p) >> 21) & 0x1f)
#define HCS_MAX_SP_LO(p)	(((p) >> 27) & 0x1f)
#define DUPLICATE_ENTRY ((u8)(-1))
#define XHCI_EXT_PORT_MAJOR(x)	(((x) >> 24) & 0xff)
#define XHCI_EXT_PORT_LP(x)	(((x) >> 14) & 0x03)
#define XHCI_EXT_PORT_COUNT(x)	(((x) >> 8) & 0xff)
#define XHCI_HLC               (1 << 19)
#define XHCI_EXT_PORT_PSIE(x)	(((x) >> 4) & 0x03)
#define XHCI_EXT_PORT_PFD(x)	(((x) >> 8) & 0x01)
#define XHCI_EXT_PORT_PLT(x)	(((x) >> 6) & 0x03)
#define XHCI_EXT_PORT_OFF(x)	((x) & 0xff)
#define XHCI_EXT_PORT_PSIM(x)	(((x) >> 16) & 0xffff)
#define XHCI_EXT_PORT_PSIV(x)	(((x) >> 0) & 0x0f)
#define XHCI_EXT_PORT_MINOR(x)	(((x) >> 16) & 0xff)
#define CTX_SIZE(_hcc)		(_hcc & HCC_64BYTE_CONTEXT ? 64 : 32)
#define XHCI_CTX_TYPE_DEVICE  0x1
#define XHCI_CTX_TYPE_INPUT   0x2

/* Runtime register offsets (derived from xHCI spec, used by driver) */
#define RUNTIME_IR_OFFSET   0x20
#define IR_IMAN 0x00
#define IR_IMOD 0x04
#define IR_ERSTSZ 0x08
#define IR_ERSTBA 0x10
#define IR_ERDP 0x18

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    struct xhci_cap_regs {
        uint32_t hc_capbase;
        uint32_t hcs_params1;
        uint32_t hcs_params2;
        uint32_t hcs_params3;
        uint32_t hcc_params;
        uint32_t db_off;
        uint32_t run_regs_off;
        uint32_t hcc_params2;
    } cap_regs;

    struct xhci_op_regs {
        uint32_t command;
        uint32_t status;
        uint32_t page_size;
        uint32_t reserved1;
        uint32_t reserved2;
        uint32_t dev_notification;
        uint64_t cmd_ring;
        uint32_t reserved3[4];
        uint64_t dcbaa_ptr;
        uint32_t config_reg;
        uint32_t reserved4[241];
        struct xhci_port_regs {
            uint32_t portsc;
            uint32_t portpmsc;
            uint32_t portli;
            uint32_t porthlmpc;
        } port_regs[2];
    } op_regs;

    struct xhci_runtime_regs {
        uint32_t mfindex;
        uint32_t rsvd[7];
        struct {
            uint32_t iman;
            uint32_t imod;
            uint32_t erstsz;
            uint32_t rsvd;
            uint64_t erstba;
            uint64_t erdp;
        } ir[1];  /* Only one interrupter supported */
    } runtime;

    struct {
        uint64_t cmd_ring_addr;
        uint64_t event_ring_addr;
        uint64_t dcbaa_ptr;
        uint32_t cmd_ring_state;
    } dma;

    bool in_reset;
    uint8_t pm_state;
};

/* Extended Capabilities chain: minimal valid chain for xhci_ext_cap_init */
static const uint32_t xhci_ext_caps[] = {
    /* USB 3.0 Protocol Capability (ID=2, NEXT=1 dword -> offset to next) */
    0x00000102,
    /* USB 2.0 Protocol Capability (ID=1, NEXT=0 -> end of chain) */
    0x00000001
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool global_enable = (s->op_regs.command & CMD_EIE) != 0;
    bool irq_pending = false;

    /* For now, only check first interrupter */
    if (global_enable && (s->runtime.ir[0].iman & IMAN_IE) &&
        (s->runtime.ir[0].iman & IMAN_IP)) {
        irq_pending = true;
    }

    if (irq_pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* No DMA implemented yet; driver DMA is done via command ring and doorbell writes */
}

/* Extended Capability read handler */
static uint32_t xhci_ext_cap_read(hwaddr offset)
{
    hwaddr idx = offset / 4;
    if (idx < ARRAY_SIZE(xhci_ext_caps)) {
        return xhci_ext_caps[idx];
    }
    return 0;
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint8_t cap_length = HC_LENGTH(s->cap_regs.hc_capbase);
    hwaddr op_base = cap_length;
    hwaddr run_base = s->cap_regs.run_regs_off;
    hwaddr doorbell_base = s->cap_regs.db_off;
    hwaddr ext_cap_base = XHCI_HCC_EXT_CAPS(s->cap_regs.hcc_params) << 2;

    /* Capability registers */
    if (addr < cap_length) {
        switch (addr) {
        case 0x00: val = s->cap_regs.hc_capbase; break;
        case 0x04: val = s->cap_regs.hcs_params1; break;
        case 0x08: val = s->cap_regs.hcs_params2; break;
        case 0x0c: val = s->cap_regs.hcs_params3; break;
        case 0x10: val = s->cap_regs.hcc_params; break;
        case 0x14: val = s->cap_regs.db_off; break;
        case 0x18: val = s->cap_regs.run_regs_off; break;
        case 0x1c: val = s->cap_regs.hcc_params2; break;
        default: break;
        }
        return val;
    }

    /* Operational registers */
    if (addr >= op_base && addr < run_base) {
        hwaddr op_addr = addr - op_base;
        /* Map to op_regs fields (assuming little-endian 32-bit accesses) */
        switch (op_addr) {
        case 0x00: val = s->op_regs.command; break;
        case 0x04: val = s->op_regs.status; break;
        case 0x08: val = s->op_regs.page_size; break;
        case 0x0c: val = s->op_regs.reserved1; break;
        case 0x10: val = s->op_regs.reserved2; break;
        case 0x14: val = s->op_regs.dev_notification; break;
        case 0x18: case 0x1c: /* cmd_ring 64-bit */
            if (size == 8) {
                val = s->op_regs.cmd_ring;
            } else if (size == 4) {
                val = (op_addr == 0x18) ? (uint32_t)s->op_regs.cmd_ring :
                                          (uint32_t)(s->op_regs.cmd_ring >> 32);
            }
            break;
        case 0x20: case 0x24: case 0x28: case 0x2c: /* reserved3 */
            if (op_addr >= 0x20 && op_addr < 0x30) {
                int idx = (op_addr - 0x20) / 4;
                val = s->op_regs.reserved3[idx];
            }
            break;
        case 0x30: case 0x34: /* dcbaa_ptr 64-bit */
            if (size == 8) {
                val = s->op_regs.dcbaa_ptr;
            } else if (size == 4) {
                val = (op_addr == 0x30) ? (uint32_t)s->op_regs.dcbaa_ptr :
                                          (uint32_t)(s->op_regs.dcbaa_ptr >> 32);
            }
            break;
        case 0x38: val = s->op_regs.config_reg; break;
        case 0x400: case 0x404: case 0x408: case 0x40c: /* port_regs[0] */
            if (op_addr >= 0x400 && op_addr < 0x410) {
                int port = 0;
                int reg_offset = op_addr - 0x400;
                switch (reg_offset) {
                case 0x00: val = s->op_regs.port_regs[port].portsc; break;
                case 0x04: val = s->op_regs.port_regs[port].portpmsc; break;
                case 0x08: val = s->op_regs.port_regs[port].portli; break;
                case 0x0c: val = s->op_regs.port_regs[port].porthlmpc; break;
                }
            }
            break;
        case 0x410: case 0x414: case 0x418: case 0x41c: /* port_regs[1] */
            if (op_addr >= 0x410 && op_addr < 0x420) {
                int port = 1;
                int reg_offset = op_addr - 0x410;
                switch (reg_offset) {
                case 0x00: val = s->op_regs.port_regs[port].portsc; break;
                case 0x04: val = s->op_regs.port_regs[port].portpmsc; break;
                case 0x08: val = s->op_regs.port_regs[port].portli; break;
                case 0x0c: val = s->op_regs.port_regs[port].porthlmpc; break;
                }
            }
            break;
        default:
            /* Other reserved operational registers return 0 */
            break;
        }
        return val;
    }

    /* Runtime registers */
    if (addr >= run_base && addr < run_base + 0x400) {
        hwaddr run_addr = addr - run_base;
        /* First interrupter only */
        if (run_addr >= RUNTIME_IR_OFFSET && run_addr < RUNTIME_IR_OFFSET + 0x20) {
            hwaddr ir_addr = run_addr - RUNTIME_IR_OFFSET;
            switch (ir_addr) {
            case IR_IMAN: val = s->runtime.ir[0].iman; break;
            case IR_IMOD: val = s->runtime.ir[0].imod; break;
            case IR_ERSTSZ: val = s->runtime.ir[0].erstsz; break;
            case IR_ERSTBA:
                if (size == 8) {
                    val = s->runtime.ir[0].erstba;
                } else if (size == 4) {
                    val = (ir_addr == IR_ERSTBA) ? (uint32_t)s->runtime.ir[0].erstba :
                                                   (uint32_t)(s->runtime.ir[0].erstba >> 32);
                }
                break;
            case IR_ERDP:
                if (size == 8) {
                    val = s->runtime.ir[0].erdp;
                } else if (size == 4) {
                    val = (ir_addr == IR_ERDP) ? (uint32_t)s->runtime.ir[0].erdp :
                                                 (uint32_t)(s->runtime.ir[0].erdp >> 32);
                }
                break;
            default: break;
            }
        }
        return val;
    }

    /* Doorbell array (write-only, reads return 0) */
    if (addr >= doorbell_base && addr < doorbell_base + 0x1000) {
        return 0;
    }

    /* Extended Capabilities */
    if (ext_cap_base && addr >= ext_cap_base) {
        return xhci_ext_cap_read(addr - ext_cap_base);
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t cap_length = HC_LENGTH(s->cap_regs.hc_capbase);
    hwaddr op_base = cap_length;
    hwaddr run_base = s->cap_regs.run_regs_off;
    hwaddr doorbell_base = s->cap_regs.db_off;

    /* Capability registers: read-only */
    if (addr < cap_length) {
        return;
    }

    /* Operational registers */
    if (addr >= op_base && addr < run_base) {
        hwaddr op_addr = addr - op_base;
        switch (op_addr) {
        case 0x00: /* command */
            s->op_regs.command = val;
            /* Handle side-effects of command register bits */
            if (val & CMD_RUN) {
                s->op_regs.status &= ~STS_HALT;   /* clear halt */
            } else {
                s->op_regs.status |= STS_HALT;
            }
            if (val & CMD_EIE) {
                /* Global interrupt enable: will be used in IRQ update */
            }
            /* Other command bits ignored */
            break;
        case 0x04: /* status */
            /* Status register is often read-only or W1C for some bits. Write 1 to clear certain bits. */
            /* The driver may write to clear STS_EINT, STS_SAVE, etc. We'll apply clear-on-write1 where appropriate. */
            s->op_regs.status &= ~(val & (STS_EINT | STS_SAVE | STS_SRE | STS_RESTORE | STS_CNR));
            /* Other bits like STS_HALT are read-only */
            break;
        case 0x08: /* page_size */
            s->op_regs.page_size = val;
            break;
        case 0x14: /* dev_notification */
            s->op_regs.dev_notification = val;
            break;
        case 0x18: case 0x1c: /* cmd_ring (64-bit) */
            if (size == 8) {
                s->op_regs.cmd_ring = val;
            } else {
                if (op_addr == 0x18) {
                    s->op_regs.cmd_ring = deposit64(s->op_regs.cmd_ring, 0, 32, val);
                } else {
                    s->op_regs.cmd_ring = deposit64(s->op_regs.cmd_ring, 32, 32, val);
                }
            }
            break;
        case 0x30: case 0x34: /* dcbaa_ptr (64-bit) */
            if (size == 8) {
                s->op_regs.dcbaa_ptr = val;
            } else {
                if (op_addr == 0x30) {
                    s->op_regs.dcbaa_ptr = deposit64(s->op_regs.dcbaa_ptr, 0, 32, val);
                } else {
                    s->op_regs.dcbaa_ptr = deposit64(s->op_regs.dcbaa_ptr, 32, 32, val);
                }
            }
            break;
        case 0x38: /* config_reg */
            s->op_regs.config_reg = val;
            break;
        case 0x400: case 0x404: case 0x408: case 0x40c: /* port_regs[0] */
            if (op_addr >= 0x400 && op_addr < 0x410) {
                int port = 0;
                int reg_offset = op_addr - 0x400;
                switch (reg_offset) {
                case 0x00: s->op_regs.port_regs[port].portsc = val; break;
                case 0x04: s->op_regs.port_regs[port].portpmsc = val; break;
                case 0x08: s->op_regs.port_regs[port].portli = val; break;
                case 0x0c: s->op_regs.port_regs[port].porthlmpc = val; break;
                }
            }
            break;
        case 0x410: case 0x414: case 0x418: case 0x41c: /* port_regs[1] */
            if (op_addr >= 0x410 && op_addr < 0x420) {
                int port = 1;
                int reg_offset = op_addr - 0x410;
                switch (reg_offset) {
                case 0x00: s->op_regs.port_regs[port].portsc = val; break;
                case 0x04: s->op_regs.port_regs[port].portpmsc = val; break;
                case 0x08: s->op_regs.port_regs[port].portli = val; break;
                case 0x0c: s->op_regs.port_regs[port].porthlmpc = val; break;
                }
            }
            break;
        default:
            /* Ignore writes to reserved operational registers */
            break;
        }
        return;
    }

    /* Runtime registers */
    if (addr >= run_base && addr < run_base + 0x400) {
        hwaddr run_addr = addr - run_base;
        if (run_addr >= RUNTIME_IR_OFFSET && run_addr < RUNTIME_IR_OFFSET + 0x20) {
            hwaddr ir_addr = run_addr - RUNTIME_IR_OFFSET;
            switch (ir_addr) {
            case IR_IMAN:
                /* IP is Write-1-to-Clear, IE is R/W */
                if (val & IMAN_IP) {
                    s->runtime.ir[0].iman &= ~IMAN_IP;
                }
                s->runtime.ir[0].iman = (s->runtime.ir[0].iman & ~IMAN_IE) | (val & IMAN_IE);
                break;
            case IR_IMOD:
                s->runtime.ir[0].imod = val;
                break;
            case IR_ERSTSZ:
                s->runtime.ir[0].erstsz = val & ERST_SIZE_MASK;
                break;
            case IR_ERSTBA:
                if (size == 8) {
                    s->runtime.ir[0].erstba = val & ERST_BASE_ADDRESS_MASK;
                } else {
                    if (ir_addr == IR_ERSTBA) {
                        s->runtime.ir[0].erstba = deposit64(s->runtime.ir[0].erstba, 0, 32, val);
                    } else {
                        s->runtime.ir[0].erstba = deposit64(s->runtime.ir[0].erstba, 32, 32, val);
                    }
                    s->runtime.ir[0].erstba &= ERST_BASE_ADDRESS_MASK;
                }
                break;
            case IR_ERDP:
                if (size == 8) {
                    s->runtime.ir[0].erdp = val & ERST_PTR_MASK;
                } else {
                    if (ir_addr == IR_ERDP) {
                        s->runtime.ir[0].erdp = deposit64(s->runtime.ir[0].erdp, 0, 32, val);
                    } else {
                        s->runtime.ir[0].erdp = deposit64(s->runtime.ir[0].erdp, 32, 32, val);
                    }
                    s->runtime.ir[0].erdp &= ERST_PTR_MASK;
                }
                break;
            default: break;
            }
            pcibase_update_irq(s);
        }
        return;
    }

    /* Doorbell array: writes are ignored for now, would trigger DMA */
    if (addr >= doorbell_base && addr < doorbell_base + 0x1000) {
        return;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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
    s->cap_regs.hc_capbase = 0x01000020;
    s->cap_regs.hcs_params1 = (2 << 24) | (8 << 8) | 8;
    s->cap_regs.hcs_params2 = 0;
    s->cap_regs.hcs_params3 = 0;
    s->cap_regs.hcc_params = HCC_64BIT_ADDR | (0x2000 << 16); /* ext caps at offset 0x2000? Change to 0x8000 for ext_caps */
    /* Fix ext caps offset to point to our array */
    s->cap_regs.hcc_params = (s->cap_regs.hcc_params & ~0xffff0000) | (0x8000 >> 2) << 16;
    s->cap_regs.db_off = 0x1000;
    s->cap_regs.run_regs_off = 0x2000;
    s->cap_regs.hcc_params2 = 0;

    s->op_regs.command = 0;
    s->op_regs.status = STS_HALT;   /* controller halted initially */
    s->op_regs.page_size = XHCI_PAGE_SIZE_MASK; /* indicate 4KB page size */
    s->op_regs.dev_notification = 0;
    s->op_regs.cmd_ring = 0;
    s->op_regs.dcbaa_ptr = 0;
    s->op_regs.config_reg = 0;
    for (int i = 0; i < 2; i++) {
        s->op_regs.port_regs[i].portsc = 0;
        s->op_regs.port_regs[i].portpmsc = 0;
        s->op_regs.port_regs[i].portli = 0;
        s->op_regs.port_regs[i].porthlmpc = 0;
    }

    s->runtime.mfindex = 0;
    memset(s->runtime.rsvd, 0, sizeof(s->runtime.rsvd));
    for (int i = 0; i < 1; i++) {
        s->runtime.ir[i].iman = 0;
        s->runtime.ir[i].imod = 0;
        s->runtime.ir[i].erstsz = 0;
        s->runtime.ir[i].erstba = 0;
        s->runtime.ir[i].erdp = 0;
    }

    s->dma.cmd_ring_addr = 0;
    s->dma.event_ring_addr = 0;
    s->dma.dcbaa_ptr = 0;
    s->dma.cmd_ring_state = 0;
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

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000;
    s->bar_info[0].name = "xhci-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI capability */
    if (msi_init(pdev, 0, 1, false, false, errp)) {
        return;
    }
    s->has_msi = true;

    /* Set initial register values */
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
}

static const VMStateDescription vmstate_pcibase = {
    .name = "xhci_hcd_pci",
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
