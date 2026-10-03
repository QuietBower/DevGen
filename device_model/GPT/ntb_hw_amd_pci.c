/*
 * QEMU 8.2.10 PCI device model for AMD NTB (behavioral model).
 * Auto-generated for driver: drivers/ntb/hw/amd/ntb_hw_amd.c
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

#define TYPE_PCIBASE_DEVICE "ntb_hw_amd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/*
 * From amd_ntb_pci_tbl (first entry only):
 *   { PCI_VDEVICE(AMD, 0x145b), ... }
 */
#define AMD_NTB_PCI_VENDOR_ID  PCI_VENDOR_ID_AMD
#define AMD_NTB_PCI_DEVICE_ID  0x145b

/*
 * The NTB driver registers as a PCI device driver without overriding
 * class code, so we use a generic bridge/other class here. The actual
 * class used by the kernel for matching is not overridden in the
 * driver source itself.
 */
#define AMD_NTB_PCI_CLASS_ID   PCI_CLASS_OTHERS

/* NTB link status helper masks from driver */
#define NTB_LNK_STA_WIDTH_MASK 0x03F00000
#define NTB_LNK_STA_SPEED_MASK 0x000F0000

#define NTB_LNK_STA_WIDTH(x)   (((x) & NTB_LNK_STA_WIDTH_MASK) >> 20)
#define NTB_LNK_STA_SPEED(x)   (((x) & NTB_LNK_STA_SPEED_MASK) >> 16)

#define AMD_LINK_HB_TIMEOUT_MS 1000

/*
 * The Linux driver uses a single BAR (BAR0) for self_mmio, and derives
 * peer_mmio as self_mmio + AMD_PEER_OFFSET. All accesses are MMIO.
 * We therefore expose a single MMIO BAR with a conservative size that
 * covers all used register offsets (up to AMD_PEER_OFFSET + highest
 * per-peer register).
 *
 * The concrete numeric values of these offsets come from the driver's
 * register header, which is not included here, so we declare minimal
 * local definitions purely to establish a register map for the QEMU
 * model. They must match the driver's header for correct operation.
 */

#define AMD_INTMASK_OFFSET     0x0000
#define AMD_INTSTAT_OFFSET     0x0004
#define AMD_DBMASK_OFFSET      0x0008
#define AMD_DBSTAT_OFFSET      0x000C
#define AMD_DBREQ_OFFSET       0x0010
#define AMD_SPAD_OFFSET        0x0100
#define AMD_SIDEINFO_OFFSET    0x0200
#define AMD_CNTL_OFFSET        0x0204
#define AMD_SMUACK_OFFSET      0x0210
#define AMD_PMESTAT_OFFSET     0x0214

#define AMD_BAR1XLAT_OFFSET    0x0300
#define AMD_BAR1LMT_OFFSET     0x0308
#define AMD_BAR23XLAT_OFFSET   0x0310
#define AMD_BAR23LMT_OFFSET    0x0318
#define AMD_BAR45XLAT_OFFSET   0x0320
#define AMD_BAR45LMT_OFFSET    0x0328

#define AMD_PEER_OFFSET        0x1000

/* Event / status bits used by the driver */
#define AMD_EVENT_INTMASK      0x0000ffffU
#define AMD_PEER_FLUSH_EVENT   0x00000001U
#define AMD_PEER_RESET_EVENT   0x00000002U
#define AMD_LINK_DOWN_EVENT    0x00000004U
#define AMD_PEER_D3_EVENT      0x00000008U
#define AMD_PEER_PMETO_EVENT   0x00000010U
#define AMD_LINK_UP_EVENT      0x00000020U
#define AMD_PEER_D0_EVENT      0x00000040U

#define AMD_SIDE_READY         0x00000002U
#define AMD_SIDE_MASK          0x00000001U

#define PMM_REG_CTL            0x00000001U
#define SMM_REG_CTL            0x00000002U

#define AMD_SPADS_CNT          64
#define AMD_DB_CNT             16
#define AMD_MSIX_VECTOR_CNT    16

/* simple bit helper for 64-bit values */
#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif
#ifndef BIT_ULL
#define BIT_ULL(nr) (1ULL << (nr))
#endif


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

/*
 * Mirror of static per-device data from the Linux driver (ntb_dev_data).
 * Only fields explicitly visible in the driver snippet are represented.
 */
typedef struct AMDNTBDevData {
    unsigned char mw_count;
    unsigned int mw_idx;
    bool is_endpoint;
} AMDNTBDevData;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows and NTB Device State (from struct amd_ntb_dev) */
    uint32_t ntb_side;
    uint32_t lnk_sta;
    uint32_t cntl_sta;
    uint32_t peer_sta;

    AMDNTBDevData *dev_data;
    unsigned char mw_count;
    unsigned char spad_count;
    unsigned char db_count;
    unsigned char msix_vec_count;

    uint64_t db_valid_mask;
    uint64_t db_mask;
    uint64_t db_last_bit;
    uint32_t int_mask;

    void *msix_entries;
    void *vec_entries;

    /* MMIO base pointers corresponding to self_mmio/peer_mmio */
    void *self_mmio;
    void *peer_mmio;
    unsigned int self_spad;
    unsigned int peer_spad;

    /* Emulated register space backing */
    uint8_t *mmio_mem;      /* pointer to BAR0 backing RAM */
    hwaddr  mmio_size;      /* size of BAR0 */

    /* Cached slices of the register space for convenience */
    uint32_t reg_intmask;
    uint32_t reg_intstat;
    uint16_t reg_dbmask;
    uint16_t reg_dbstat;
    uint16_t reg_dbreq;
    uint32_t reg_sideinfo_self;
    uint32_t reg_sideinfo_peer;
    uint32_t reg_cntl;
    uint32_t reg_smuack;
    uint32_t reg_pmestat_peer;

    uint64_t reg_bar1xlat_peer;
    uint32_t reg_bar1lmt_peer;
    uint64_t reg_bar23xlat_peer;
    uint64_t reg_bar23lmt_peer;
    uint64_t reg_bar45xlat_peer;
    uint64_t reg_bar45lmt_peer;
};

static inline uint32_t pcibase_mmio_read32(PCIBaseState *s, hwaddr addr)
{
    if (addr + 4 > s->mmio_size) {
        return 0;
    }
    return le32_to_cpu(ldl_le_p((uint32_t *)(s->mmio_mem + addr)));
}

static inline void pcibase_mmio_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (addr + 4 > s->mmio_size) {
        return;
    }
    stl_le_p(s->mmio_mem + addr, val);
}

static inline uint16_t pcibase_mmio_read16(PCIBaseState *s, hwaddr addr)
{
    if (addr + 2 > s->mmio_size) {
        return 0;
    }
    return le16_to_cpu(lduw_le_p(s->mmio_mem + addr));
}

static inline void pcibase_mmio_write16(PCIBaseState *s, hwaddr addr, uint16_t val)
{
    if (addr + 2 > s->mmio_size) {
        return;
    }
    stw_le_p(s->mmio_mem + addr, val);
}

static inline uint64_t pcibase_mmio_read64(PCIBaseState *s, hwaddr addr)
{
    if (addr + 8 > s->mmio_size) {
        return 0;
    }
    return le64_to_cpu(ldq_le_p((uint64_t *)(s->mmio_mem + addr)));
}

static inline void pcibase_mmio_write64(PCIBaseState *s, hwaddr addr, uint64_t val)
{
    if (addr + 8 > s->mmio_size) {
        return;
    }
    stq_le_p(s->mmio_mem + addr, val);
}

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->reg_intstat & s->reg_intmask & AMD_EVENT_INTMASK) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Generate a synthetic link status value with non-zero speed/width */
static uint32_t pcibase_default_lnk_sta(void)
{
    uint32_t speed = 3; /* Gen3 */
    uint32_t width = 8; /* x8 */
    return ((width << 20) & NTB_LNK_STA_WIDTH_MASK) |
           ((speed << 16) & NTB_LNK_STA_SPEED_MASK);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= s->mmio_size) {
        return 0;
    }

    if (size == 1) {
        val = s->mmio_mem[addr];
        return val;
    }

    if (addr < AMD_PEER_OFFSET) {
        /* self_mmio space */
        switch (addr) {
        case AMD_INTMASK_OFFSET:
            if (size == 4) {
                val = s->reg_intmask;
            }
            break;
        case AMD_INTSTAT_OFFSET:
            if (size == 4) {
                val = s->reg_intstat;
            }
            break;
        case AMD_DBMASK_OFFSET:
            if (size == 2) {
                val = s->reg_dbmask;
            } else if (size == 4) {
                val = s->reg_dbmask;
            }
            break;
        case AMD_DBSTAT_OFFSET:
            if (size == 2) {
                val = s->reg_dbstat;
            } else if (size == 4) {
                val = s->reg_dbstat;
            }
            break;
        case AMD_DBREQ_OFFSET:
            if (size == 2) {
                val = s->reg_dbreq;
            } else if (size == 4) {
                val = s->reg_dbreq;
            }
            break;
        case AMD_SIDEINFO_OFFSET:
            if (size == 4) {
                val = s->reg_sideinfo_self;
            }
            break;
        case AMD_CNTL_OFFSET:
            if (size == 4) {
                val = s->reg_cntl;
            }
            break;
        case AMD_SMUACK_OFFSET:
            if (size == 4) {
                val = s->reg_smuack;
            }
            break;
        case AMD_PMESTAT_OFFSET:
            if (size == 4) {
                /* self PMESTAT currently unused */
                val = 0;
            }
            break;
        default:
            /* scratchpads and BAR XLAT/LMT */
            if (addr >= AMD_SPAD_OFFSET &&
                addr < AMD_SPAD_OFFSET + AMD_SPADS_CNT * 4) {
                if (size == 4) {
                    val = pcibase_mmio_read32(s, addr);
                }
            } else if (addr == AMD_BAR1XLAT_OFFSET && size == 8) {
                val = pcibase_mmio_read64(s, addr);
            } else if (addr == AMD_BAR23XLAT_OFFSET && size == 8) {
                val = pcibase_mmio_read64(s, addr);
            } else if (addr == AMD_BAR45XLAT_OFFSET && size == 8) {
                val = pcibase_mmio_read64(s, addr);
            } else if (addr == AMD_BAR1LMT_OFFSET && size == 4) {
                val = pcibase_mmio_read32(s, addr);
            } else if (addr == AMD_BAR23LMT_OFFSET && size == 8) {
                val = pcibase_mmio_read64(s, addr);
            } else if (addr == AMD_BAR45LMT_OFFSET && size == 8) {
                val = pcibase_mmio_read64(s, addr);
            } else {
                /* generic fallback */
                if (size == 2) {
                    val = pcibase_mmio_read16(s, addr);
                } else if (size == 4) {
                    val = pcibase_mmio_read32(s, addr);
                } else if (size == 8) {
                    val = pcibase_mmio_read64(s, addr);
                }
            }
            break;
        }
    } else {
        /* peer_mmio space */
        hwaddr poff = addr - AMD_PEER_OFFSET;
        switch (poff) {
        case AMD_SIDEINFO_OFFSET:
            if (size == 4) {
                val = s->reg_sideinfo_peer;
            }
            break;
        case AMD_PMESTAT_OFFSET:
            if (size == 4) {
                val = s->reg_pmestat_peer;
            }
            break;
        case AMD_BAR1XLAT_OFFSET:
            if (size == 8) {
                val = s->reg_bar1xlat_peer;
            }
            break;
        case AMD_BAR1LMT_OFFSET:
            if (size == 4) {
                val = s->reg_bar1lmt_peer;
            }
            break;
        case AMD_BAR23XLAT_OFFSET:
            if (size == 8) {
                val = s->reg_bar23xlat_peer;
            }
            break;
        case AMD_BAR23LMT_OFFSET:
            if (size == 8) {
                val = s->reg_bar23lmt_peer;
            }
            break;
        case AMD_BAR45XLAT_OFFSET:
            if (size == 8) {
                val = s->reg_bar45xlat_peer;
            }
            break;
        case AMD_BAR45LMT_OFFSET:
            if (size == 8) {
                val = s->reg_bar45lmt_peer;
            }
            break;
        default:
            if (poff >= AMD_SPAD_OFFSET &&
                poff < AMD_SPAD_OFFSET + AMD_SPADS_CNT * 4 &&
                size == 4) {
                val = pcibase_mmio_read32(s, addr);
            } else {
                if (size == 2) {
                    val = pcibase_mmio_read16(s, addr);
                } else if (size == 4) {
                    val = pcibase_mmio_read32(s, addr);
                } else if (size == 8) {
                    val = pcibase_mmio_read64(s, addr);
                }
            }
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= s->mmio_size) {
        return;
    }

    if (size == 1) {
        s->mmio_mem[addr] = (uint8_t)val;
        return;
    }

    if (addr < AMD_PEER_OFFSET) {
        /* self_mmio */
        switch (addr) {
        case AMD_INTMASK_OFFSET:
            if (size == 4) {
                s->int_mask = (uint32_t)val;
                s->reg_intmask = (uint32_t)val;
                pcibase_mmio_write32(s, addr, s->reg_intmask);
                pcibase_update_irq(s);
            }
            break;
        case AMD_INTSTAT_OFFSET:
            if (size == 4) {
                /* W1C: clear bits written as 1 */
                uint32_t w = (uint32_t)val;
                s->reg_intstat &= ~w;
                pcibase_mmio_write32(s, addr, s->reg_intstat);
                pcibase_update_irq(s);
            }
            break;
        case AMD_DBMASK_OFFSET:
            if (size == 2 || size == 4) {
                s->reg_dbmask = (uint16_t)val;
                pcibase_mmio_write16(s, addr, s->reg_dbmask);
            }
            break;
        case AMD_DBSTAT_OFFSET:
            if (size == 2 || size == 4) {
                /* W1C for DBSTAT */
                uint16_t w = (uint16_t)val;
                s->reg_dbstat &= ~w;
                pcibase_mmio_write16(s, addr, s->reg_dbstat);
            }
            break;
        case AMD_DBREQ_OFFSET:
            if (size == 2 || size == 4) {
                /* writing causes doorbell on peer side; store in req */
                s->reg_dbreq = (uint16_t)val;
                pcibase_mmio_write16(s, addr, s->reg_dbreq);
                /* set DBSTAT bits and possibly interrupt */
                s->reg_dbstat |= s->reg_dbreq;
                pcibase_mmio_write16(s, AMD_DBSTAT_OFFSET, s->reg_dbstat);
                /* generate interrupt as DB event */
                s->reg_intstat |= AMD_PEER_FLUSH_EVENT; /* doorbell event cause */
                pcibase_mmio_write32(s, AMD_INTSTAT_OFFSET, s->reg_intstat);
                pcibase_update_irq(s);
            }
            break;
        case AMD_SIDEINFO_OFFSET:
            if (size == 4) {
                s->reg_sideinfo_self = (uint32_t)val;
                pcibase_mmio_write32(s, addr, s->reg_sideinfo_self);
            }
            break;
        case AMD_CNTL_OFFSET:
            if (size == 4) {
                s->reg_cntl = (uint32_t)val;
                pcibase_mmio_write32(s, addr, s->reg_cntl);
            }
            break;
        case AMD_SMUACK_OFFSET:
            if (size == 4) {
                s->reg_smuack |= (uint32_t)val;
                pcibase_mmio_write32(s, addr, s->reg_smuack);
            }
            break;
        default:
            if (addr >= AMD_SPAD_OFFSET &&
                addr < AMD_SPAD_OFFSET + AMD_SPADS_CNT * 4 &&
                size == 4) {
                pcibase_mmio_write32(s, addr, (uint32_t)val);
            } else if (addr == AMD_BAR1XLAT_OFFSET && size == 8) {
                pcibase_mmio_write64(s, addr, val);
            } else if (addr == AMD_BAR23XLAT_OFFSET && size == 8) {
                pcibase_mmio_write64(s, addr, val);
            } else if (addr == AMD_BAR45XLAT_OFFSET && size == 8) {
                pcibase_mmio_write64(s, addr, val);
            } else if (addr == AMD_BAR1LMT_OFFSET && size == 4) {
                pcibase_mmio_write32(s, addr, (uint32_t)val);
            } else if (addr == AMD_BAR23LMT_OFFSET && size == 8) {
                pcibase_mmio_write64(s, addr, val);
            } else if (addr == AMD_BAR45LMT_OFFSET && size == 8) {
                pcibase_mmio_write64(s, addr, val);
            } else {
                if (size == 2) {
                    pcibase_mmio_write16(s, addr, (uint16_t)val);
                } else if (size == 4) {
                    pcibase_mmio_write32(s, addr, (uint32_t)val);
                } else if (size == 8) {
                    pcibase_mmio_write64(s, addr, val);
                }
            }
            break;
        }
    } else {
        /* peer_mmio */
        hwaddr poff = addr - AMD_PEER_OFFSET;
        switch (poff) {
        case AMD_SIDEINFO_OFFSET:
            if (size == 4) {
                s->reg_sideinfo_peer = (uint32_t)val;
                pcibase_mmio_write32(s, addr, s->reg_sideinfo_peer);
            }
            break;
        case AMD_PMESTAT_OFFSET:
            if (size == 4) {
                s->reg_pmestat_peer = (uint32_t)val;
                pcibase_mmio_write32(s, addr, s->reg_pmestat_peer);
            }
            break;
        case AMD_BAR1XLAT_OFFSET:
            if (size == 8) {
                s->reg_bar1xlat_peer = val;
                pcibase_mmio_write64(s, addr, val);
            }
            break;
        case AMD_BAR1LMT_OFFSET:
            if (size == 4) {
                s->reg_bar1lmt_peer = (uint32_t)val;
                pcibase_mmio_write32(s, addr, s->reg_bar1lmt_peer);
            }
            break;
        case AMD_BAR23XLAT_OFFSET:
            if (size == 8) {
                s->reg_bar23xlat_peer = val;
                pcibase_mmio_write64(s, addr, val);
            }
            break;
        case AMD_BAR23LMT_OFFSET:
            if (size == 8) {
                s->reg_bar23lmt_peer = val;
                pcibase_mmio_write64(s, addr, val);
            }
            break;
        case AMD_BAR45XLAT_OFFSET:
            if (size == 8) {
                s->reg_bar45xlat_peer = val;
                pcibase_mmio_write64(s, addr, val);
            }
            break;
        case AMD_BAR45LMT_OFFSET:
            if (size == 8) {
                s->reg_bar45lmt_peer = val;
                pcibase_mmio_write64(s, addr, val);
            }
            break;
        default:
            if (poff >= AMD_SPAD_OFFSET &&
                poff < AMD_SPAD_OFFSET + AMD_SPADS_CNT * 4 &&
                size == 4) {
                pcibase_mmio_write32(s, addr, (uint32_t)val);
            } else {
                if (size == 2) {
                    pcibase_mmio_write16(s, addr, (uint16_t)val);
                } else if (size == 4) {
                    pcibase_mmio_write32(s, addr, (uint32_t)val);
                } else if (size == 8) {
                    pcibase_mmio_write64(s, addr, val);
                }
            }
            break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    s->lnk_sta = pcibase_default_lnk_sta();
    s->cntl_sta = 0;
    s->peer_sta = 0;

    s->reg_intmask = AMD_EVENT_INTMASK;
    s->int_mask = s->reg_intmask;
    s->reg_intstat = 0;
    s->reg_dbmask = 0xffff;
    s->reg_dbstat = 0;
    s->reg_dbreq = 0;
    s->reg_sideinfo_self = 0;
    s->reg_sideinfo_peer = 0;
    s->reg_cntl = 0;
    s->reg_smuack = 0;
    s->reg_pmestat_peer = 0;

    s->reg_bar1xlat_peer = 0;
    s->reg_bar1lmt_peer = 0;
    s->reg_bar23xlat_peer = 0;
    s->reg_bar23lmt_peer = 0;
    s->reg_bar45xlat_peer = 0;
    s->reg_bar45lmt_peer = 0;

    if (s->mmio_mem && s->mmio_size) {
        memset(s->mmio_mem, 0, s->mmio_size);
    }

    pcibase_update_irq(s);
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  AMD_NTB_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  AMD_NTB_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, AMD_NTB_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* One MMIO BAR (BAR0) that backs self_mmio and peer_mmio */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    /* Space for self (0..AMD_PEER_OFFSET-1) + peer space of same size */
    s->bar_info[0].size = AMD_PEER_OFFSET * 2;
    s->bar_info[0].name = "amd-ntb-mmio";

    s->mmio_size = s->bar_info[0].size;
    s->mmio_mem = g_malloc0(s->mmio_size);

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI/MSI-X capability; the driver prefers MSIX */
    if (!msix_init_exclusive_bar(pdev, AMD_MSIX_VECTOR_CNT, 0, errp)) {
        s->has_msix = true;
    } else {
        s->has_msix = false;
    }

    if (!s->has_msix) {
        if (!msi_init(pdev, 0, 1, true, false, errp)) {
            s->has_msi = true;
        } else {
            s->has_msi = false;
        }
    } else {
        s->has_msi = false;
    }

    s->mw_count = 0;
    s->spad_count = AMD_SPADS_CNT;
    s->db_count = AMD_DB_CNT;
    s->msix_vec_count = AMD_MSIX_VECTOR_CNT;

    s->db_valid_mask = BIT_ULL(s->db_count) - 1;
    s->db_mask = s->db_valid_mask;
    s->db_last_bit = s->db_count - 1;

    s->self_spad = 0;
    s->peer_spad = AMD_SPADS_CNT / 2;

    s->self_mmio = NULL;
    s->peer_mmio = NULL;

    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    if (s->mmio_mem) {
        g_free(s->mmio_mem);
        s->mmio_mem = NULL;
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ntb_hw_amd_pci",
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
